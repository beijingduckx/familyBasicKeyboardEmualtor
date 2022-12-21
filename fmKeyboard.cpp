//
// Family basic keyboard emulator
//  - Windows side program
//

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_sdlrenderer.h"

#include <SDL.h>
#include <SDL_syswm.h>

#define _POSIX_SOURCE
#include <fcntl.h>
#include <io.h>

#include <stdint.h>
#include <stdio.h>
#include <Windows.h>
#include <commdlg.h>
#include <libusb.h>
#include <assert.h>
#include <map>
#include "keymap.h"

// Built-in firmware hex strings.
static const char* firmware[] = {
#include "fbKeyEmu.inc"
	NULL };

void finalize(void);

#define _MAKE_TITLE(A)  A##"Family Basic Keyboard Emulator"
#define APP_TITLE      _MAKE_TITLE(L)
#define APP_TITLE_U8   _MAKE_TITLE(u8)

#define EVENT_TAPE_END  0
#define EVENT_UPDATE_SCREEN 1

enum tape_mode_t {
	TAPE_MODE_NONE = 0,
	TAPE_MODE_LOAD = 1,
	TAPE_MODE_SAVE = 2,
};

HWND h_main_window = NULL;

static char message[260];
static WCHAR file_name[260];
static uint32_t ui_change_event;
static bool wait_for_save = false;

static uint32_t file_size = 0;
static uint32_t current_file_pos = 0;
static tape_mode_t tape_mode = TAPE_MODE_NONE;

//======================================================================
// USB
//======================================================================
#define VID 0x04b4
#define PID 0x8613
#define OUT_COMMAND_EP (1)

#define IN_TAPE_EP (0x86)
#define OUT_TAPE_EP  (0x04)
#define RX_SIZE (63)

#define COMMAND_TRANSFER (0)
#define TAPE_TRANSFER   (1)

#define COMMAND_KEY     0
#define COMMAND_SAVE    1
#define COMMAND_LOAD    2
#define COMMAND_STOP    3
#define COMMAND_FORCE_STOP 4

static HANDLE h_usb_thread;

static libusb_device_handle* usb_handle = NULL;

static volatile bool usb_run_flag = true;
static volatile bool tape_run_flag = false;
static volatile int ui_run_flag = 1;

static struct libusb_transfer* tape_transfer;
static struct libusb_transfer* command_transfer;

//----------------------------------------------------------------------
// USB write RAM
//----------------------------------------------------------------------
#define USB_WRITE_RAM_MAX_SIZE 64
int usb_write_ram(int addr, uint8_t* dat, int size) {

	for (int i = 0; i < size; i += USB_WRITE_RAM_MAX_SIZE) {
		LONG len = (size - i > USB_WRITE_RAM_MAX_SIZE) ? USB_WRITE_RAM_MAX_SIZE : size - i;
		int ret = libusb_control_transfer(usb_handle, LIBUSB_REQUEST_TYPE_VENDOR, 0xa0, addr + i, 0, dat + i, len, 1000);
		if (ret < 0) {
			fprintf(stderr, "USB: Write Ram at %04x (len %d) failed.\n", addr + i, len);
			return -1;
		}
	}
	return 0;
}

//----------------------------------------------------------------------
// USB load firmware
//----------------------------------------------------------------------
#define FIRMWARE_MAX_SIZE_PER_LINE 64
static uint8_t firmware_dat[FIRMWARE_MAX_SIZE_PER_LINE];
int usb_load_firmware(const char* firmware[]) {
	int ret;

	// Take the CPU into RESET
	uint8_t dat = 1;
	ret = usb_write_ram(0xe600, &dat, sizeof(dat));
	if (ret < 0) {
		return -1;
	}

	// Load firmware
	int size, addr, record_type, tmp_dat;
	for (int i = 0; firmware[i] != NULL; i++) {
		const char* p = firmware[i] + 1;

		// Extract size
		ret = sscanf(p, "%2x", &size);
		assert(ret != 0);
		assert(size <= FIRMWARE_MAX_SIZE_PER_LINE);
		p += 2;

		// Extract addr
		ret = sscanf(p, "%4x", &addr);
		assert(ret != 0);
		p += 4;

		// Extract record type
		ret = sscanf(p, "%2x", &record_type);
		assert(ret != 0);
		p += 2;

		// Write program to EZ-USB's RAM (record_type==0).
		if (record_type == 0) {
			for (int j = 0; j < size; j++) {
				ret = sscanf(p, "%2x", &tmp_dat);
				firmware_dat[j] = tmp_dat & 0xff;
				assert(ret != 0);
				p += 2;
			}

			ret = usb_write_ram(addr, firmware_dat, size);
			if (ret < 0) {
				return -1;
			}
		}
	}

	// Take the CPU out of RESET (run)
	dat = 0;
	ret = usb_write_ram(0xe600, &dat, sizeof(dat));
	if (ret < 0) {
		return -1;
	}

	return 0;
}


static void __stdcall usb_callback(struct libusb_transfer* xfr) {
	static int recv_count = 0;
	int owner = *((int*)xfr->user_data);

	switch (xfr->status) {
	case LIBUSB_TRANSFER_COMPLETED:
		break;
	case LIBUSB_TRANSFER_ERROR:
		::MessageBox(h_main_window, L"Transfer error", APP_TITLE, MB_OK);
		fprintf(stderr, "USB: transfer error.\n");
		break;
	case LIBUSB_TRANSFER_TIMED_OUT:
		::MessageBox(h_main_window, L"Transfer time out", APP_TITLE, MB_OK);
		fprintf(stderr, "USB: transfer timed out.\n");
		break;
	case LIBUSB_TRANSFER_OVERFLOW:
		fprintf(stderr, "USB: transfer overflow.\n");
		break;
	case LIBUSB_TRANSFER_CANCELLED:
	case LIBUSB_TRANSFER_NO_DEVICE:
		::MessageBox(h_main_window, L"Disconnected", APP_TITLE, MB_OK);
	default:
		return;
	}
}

//----------------------------------------------------------------------
// USB thread for bulk-in transfer
//----------------------------------------------------------------------
#define READ_CHUNK_SIZE   64
#define USB_TIMEOUT_MS    3000

DWORD WINAPI usb_run(void* arg) {
	uint8_t trans_data[512];
	int actual_length = 0;
	int transfer_owner = TAPE_TRANSFER;
	int current_tape_mode = *((int*)arg);
	ULONGLONG prev_time = 0;
	char mb_file_name[MAX_PATH];

	wcstombs(mb_file_name, file_name, sizeof(mb_file_name));

	SDL_RWops* io;

	if (current_tape_mode == TAPE_MODE_LOAD) {
		io = SDL_RWFromFile(mb_file_name, "rb");
	}
	else {
		io = SDL_RWFromFile(mb_file_name, "wb");
	}
	if (io == NULL) {
		::MessageBox(NULL, L"Failed to open tape file", APP_TITLE, MB_OK);
		return 0;
	}

	if (current_tape_mode == TAPE_MODE_LOAD) {
		file_size = SDL_RWsize(io);
		SDL_RWseek(io, 0, RW_SEEK_SET);
	}
	else {
		file_size = 0;
	}

	current_file_pos = 0;
	SDL_Event event;
	SDL_memset(&event, 0, sizeof(event));
	event.type = ui_change_event;

	while (tape_run_flag) {
		if (current_tape_mode == TAPE_MODE_SAVE) {
			libusb_fill_bulk_transfer(tape_transfer, usb_handle, IN_TAPE_EP, trans_data,
				sizeof(trans_data), usb_callback, &tape_transfer, USB_TIMEOUT_MS);
		}
		else {
			DWORD dwRead = 0;
			dwRead = SDL_RWread(io, trans_data, 1, READ_CHUNK_SIZE);
			if (dwRead == 0) {
				break;
			}
			libusb_fill_bulk_transfer(tape_transfer, usb_handle, OUT_TAPE_EP, trans_data,
				dwRead, usb_callback, &tape_transfer, USB_TIMEOUT_MS);

			current_file_pos += dwRead;
			snprintf(message, sizeof(message), "Loading.. (To make keyboard work, stop loading)");
		}

		int ret = libusb_submit_transfer(tape_transfer);

		int completed = 0;
		libusb_handle_events_completed(NULL, &completed);

		if (current_tape_mode == TAPE_MODE_SAVE) {
			DWORD dwWritten = 0;
			dwWritten = SDL_RWwrite(io, tape_transfer->buffer, 1, tape_transfer->actual_length);
			if (tape_transfer->actual_length != 0 && dwWritten == 0) {
				::MessageBox(NULL, L"Failed to write", APP_TITLE, MB_OK);
			}
			current_file_pos += dwWritten;
			snprintf(message, sizeof(message), "Saving..(To make keyboard work, stop saving) %d", current_file_pos);
		}

		if ((GetTickCount64() - prev_time) > 60) {
			prev_time = GetTickCount64();
			event.user.code = EVENT_UPDATE_SCREEN;
			SDL_PushEvent(&event);
		}
	}
	SDL_RWclose(io);

	event.user.code = EVENT_TAPE_END;
	SDL_PushEvent(&event);

	return 0;
}


void send_command(uint8_t command, uint8_t* data, LONG length)
{
	int actual_length = 0;
	uint8_t* trans_data = (uint8_t*)alloca(length + 1);
	static int transfer_owner = COMMAND_TRANSFER;

	trans_data[0] = command;
	if (length != 0)
		memcpy(trans_data + 1, data, length);

	libusb_fill_bulk_transfer(command_transfer, usb_handle, OUT_COMMAND_EP, trans_data,
		length + 1, usb_callback, &transfer_owner, USB_TIMEOUT_MS);
	int ret = libusb_submit_transfer(command_transfer);
	int completed = 0;
	libusb_handle_events_completed(NULL, &completed);
}

uint8_t keybuf[20];

void init_keybuf(void)
{
	for (int i = 0; i < sizeof(keybuf); i++)
		keybuf[i] = 0;
}

void set_keyboard(int row, int bit)
{
	if (bit < 4) {
		keybuf[row] |= 1 << (bit + 4);
	}
	else {
		keybuf[10 + row] |= 1 << (bit);
	}
	send_command(COMMAND_KEY, keybuf, sizeof(keybuf));
}

void unset_keyboard(int row, int bit)
{
	if (bit < 4) {
		keybuf[row] &= (~(1 << (bit + 4))) & 0xff;
	}
	else {
		keybuf[10 + row] &= (~(1 << bit)) & 0xff;
	}
	send_command(COMMAND_KEY, keybuf, sizeof(keybuf));
}

void start_load(void)
{
	if (tape_run_flag == false) {
		send_command(COMMAND_LOAD, NULL, 0);
		tape_run_flag = true;
		tape_mode = TAPE_MODE_LOAD;
		h_usb_thread = ::CreateThread(NULL, 0, usb_run, &tape_mode, 0, NULL);
	}
}

void start_save(void)
{
	if (tape_run_flag == false) {
		send_command(COMMAND_SAVE, NULL, 0);
		tape_run_flag = true;
		tape_mode = TAPE_MODE_SAVE;
		h_usb_thread = ::CreateThread(NULL, 0, usb_run, &tape_mode, 0, NULL);
	}
}

void stop_tape(void)
{
	tape_run_flag = false;
	::WaitForSingleObject(h_usb_thread, INFINITE);
	::Sleep(500);   // wait for EZ-USB to complete OUT process
	send_command(COMMAND_STOP, NULL, 0);

	snprintf(message, sizeof(message), "Current mode: Keyboard");
}

void force_stop(void)
{
	send_command(COMMAND_FORCE_STOP, NULL, 0);
}

void handle_start_load(void)
{
	OPENFILENAME ofn;

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = h_main_window;
	ofn.lpstrFile = file_name;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(file_name);
	ofn.lpstrFilter = L"All\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (::GetOpenFileName(&ofn) == TRUE) {
		start_load();
	}
}

void handle_start_save(void)
{
	OPENFILENAME ofn;

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = h_main_window;
	ofn.lpstrFile = file_name;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(file_name);
	ofn.lpstrFilter = L"All\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_CREATEPROMPT | OFN_OVERWRITEPROMPT;

	if (::GetSaveFileName(&ofn) == TRUE) {
		snprintf(message, sizeof(message), "Ready.. Start FC side save then press 'Start save' button");
		wait_for_save = true;
	}
}

std::map<SDL_Keycode, FmKeyLoc_t> key_map;

DWORD WINAPI draw_run(void* arg) {
	// The window we'll be rendering to
	SDL_Window* window = NULL;

	// The surface contained by the window
	SDL_Renderer* Renderer = NULL;

	auto set_title = [&]() {
		char tmp[100];
		snprintf(tmp, sizeof(tmp), APP_TITLE_U8);
		SDL_SetWindowTitle(window, tmp);
	};

	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		::MessageBox(NULL, L"SDL could not initialize", APP_TITLE, MB_OK);
		return -1;
	}
	// Create window
	window = SDL_CreateWindow(APP_TITLE_U8, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		::MessageBox(NULL, L"Window could not be created", APP_TITLE, MB_OK);
		return -1;
	}

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(window, &info);
	h_main_window = info.info.win.window;

	ui_change_event = SDL_RegisterEvents(1);

	Renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	if (Renderer == NULL) {
		::MessageBox(NULL, L"Coould not create renderer", APP_TITLE, MB_OK);
		return -1;
	}
	SDL_SetRenderDrawColor(Renderer, 0x00, 0x00, 0x00, 0xff);
	SDL_RenderClear(Renderer);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	//	ImGuiIO& io = ImGui::GetIO(); (void)io;

		//ImGui::StyleColorsDark();
	ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForSDLRenderer(window, Renderer);
	ImGui_ImplSDLRenderer_Init(Renderer);

	SDL_Event e;
	snprintf(message, sizeof(message), "Current mode: Keyboard");

	while (ui_run_flag) {

		SDL_WaitEvent(&e);
		//		SDL_PollEvent(&e);
		ImGui_ImplSDL2_ProcessEvent(&e);
		if (e.type == SDL_QUIT) {
			ui_run_flag = 0;
		}
		else if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
			auto it = key_map.find(e.key.keysym.sym);
			if (it != key_map.end()) {
				set_keyboard(it->second.row, it->second.bitpos);
			}
		}
		else if (e.type == SDL_KEYUP) {
			auto it = key_map.find(e.key.keysym.sym);
			if (it != key_map.end()) {
				unset_keyboard(it->second.row, it->second.bitpos);
			}
		}
		else if (e.type == SDL_USEREVENT) {
			if (e.user.code == EVENT_TAPE_END) {
				stop_tape();
			}
		}
		ImGui_ImplSDLRenderer_NewFrame();
		ImGui_ImplSDL2_NewFrame();

		ImGui::NewFrame();

		const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + 10, main_viewport->WorkPos.y + 30), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(main_viewport->WorkSize.x / 2.0, main_viewport->WorkSize.y / 2.0), ImGuiCond_FirstUseEver);

		ImGui::Begin("Message");

		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File", !tape_run_flag)) {
				if (ImGui::MenuItem("Load..")) {
					handle_start_load();
				}
				if (ImGui::MenuItem("Save..")) {
					handle_start_save();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		ImGui::Text(message);
		if (tape_run_flag == true && file_size > 0) {
			ImGui::ProgressBar((float)current_file_pos / file_size);
		}
		if (tape_run_flag == true && ImGui::Button("Stop")) {
			stop_tape();
		}
		if (wait_for_save == true && ImGui::Button("Start Save")) {
			wait_for_save = false;
			start_save();
		}
		ImGui::End();

		ImGui::Render();
		SDL_RenderClear(Renderer);
		ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
		SDL_RenderPresent(Renderer);
	}

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}


//======================================================================
// Main
//======================================================================
int main(int argc, char* argv[]) {
	setvbuf(stdout, (char*)NULL, _IONBF, 0);
	int ret;

	// Initialize keymap
	for (KeyMap_t m : gKeyMap) {
		key_map.insert(std::make_pair(m.key, m.Loc));
	}

	// Initialize USB
	ret = libusb_init(NULL);
	ret = libusb_set_option(NULL, LIBUSB_OPTION_USE_USBDK);
	usb_handle = libusb_open_device_with_vid_pid(NULL, VID, PID);

	if (usb_handle == NULL) {
		::MessageBox(NULL, L"EZ-USB is not connected.", APP_TITLE, MB_OK);
		return -1;
	}
	ret = libusb_claim_interface(usb_handle, 0);
	ret = libusb_set_interface_alt_setting(usb_handle, 0, 1);
	if (ret < 0) {
		::MessageBox(NULL, L"USB interface not found", APP_TITLE, MB_OK);
		return -1;
	}
	command_transfer = libusb_alloc_transfer(0);
	tape_transfer = libusb_alloc_transfer(0);

	if (command_transfer == NULL || tape_transfer == NULL) {
		::MessageBox(NULL, L"Failed to allocate transfer", APP_TITLE, MB_OK);
		libusb_close(usb_handle);
		return -1;
	}

	// load firmware
	//printf("Main: Firmware download...");
	if (usb_load_firmware(firmware) >= 0) {
		//puts("finished.");
	}
	else {
		::MessageBox(NULL, L"Firmware downloading failed.", APP_TITLE, MB_OK);
		return -1;
	}

	draw_run(NULL);

	finalize();
	
	return 0;
}

void finalize() {
	usb_run_flag = 0;

	if (::WaitForSingleObject(h_usb_thread, 1000) != WAIT_OBJECT_0) {
		//perror("Main: Failed to join USB thread.");
	}
	else {
		//puts("Main: USB thread joined.");
	}

	CloseHandle(h_usb_thread);
}
