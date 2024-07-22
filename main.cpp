#include <windows.h>
#include <xinput.h>
#include "nya_commontimer.h"
#include "nya_commonhooklib.h"

enum eControllerInput {
	INPUT_HANDBRAKE = 0,
	INPUT_NITRO = 1,
	INPUT_CAMERA = 3,
	INPUT_LOOK_BEHIND = 4,
	INPUT_RESET = 5,
	INPUT_PLAYERLIST = 7,
	INPUT_MENU_ACCEPT = 8,
	INPUT_PAUSE = 9,
	INPUT_ACCELERATE = 10,
	INPUT_BRAKE = 11,
	INPUT_STEER_LEFT = 12,
	INPUT_STEER_RIGHT = 13,
	INPUT_GEAR_DOWN = 18,
	INPUT_GEAR_UP = 19,
};

// helper functions
// XInputGetState without statically linking, from imgui :3
DWORD XInputGetState_Dynamic(int dwUserIndex, XINPUT_STATE* pState) {
	static auto funcPtr = (DWORD(WINAPI*)(DWORD, XINPUT_STATE*))nullptr;
	if (!funcPtr) {
		const char* dlls[] = {
				"xinput1_4.dll",
				"xinput1_3.dll",
				"xinput9_1_0.dll",
				"xinput1_2.dll",
				"xinput1_1.dll"
		};
		for (auto& file : dlls) {
			if (auto dll = LoadLibraryA(file)) {
				funcPtr = (DWORD(WINAPI*)(DWORD, XINPUT_STATE*))GetProcAddress(dll, "XInputGetState");
				break;
			}
		}
	}
	if (funcPtr) return funcPtr(dwUserIndex, pState);
	return 1;
}

XINPUT_STATE gPadLastState[XUSER_MAX_COUNT];
XINPUT_STATE gPadState[XUSER_MAX_COUNT];

auto EndSceneOrig = (HRESULT(__thiscall*)(void*))nullptr;
HRESULT __fastcall EndSceneHook(void* a1) {
	for (int i = 0; i < XUSER_MAX_COUNT; i++) {
		gPadLastState[i] = gPadState[i];

		XINPUT_STATE state;
		memset(&state, 0, sizeof(state));
		if (XInputGetState_Dynamic(i, &state) == ERROR_SUCCESS) {
			gPadState[i] = state;
		}
		else {
			memset(&gPadState[i], 0, sizeof(gPadState[i]));
		}
	}
	return EndSceneOrig(a1);
}

double fButtonRemapTimeout = false;
bool bControllerSupportDisabledForRemapping = false;
int nMenuInputReturnValue = 0;
int __fastcall GetMenuInputs(int inputId) {
	auto keyboard = (uint8_t*)0x8D7D60;
	if (keyboard[inputId]) return keyboard[inputId];

	if (bControllerSupportDisabledForRemapping) {
		fButtonRemapTimeout = 0.2;
		return 0;
	}

	int controllerInput = 0;
	switch (inputId) {
		case VK_LEFT:
			controllerInput = XINPUT_GAMEPAD_DPAD_LEFT;
			break;
		case VK_RIGHT:
			controllerInput = XINPUT_GAMEPAD_DPAD_RIGHT;
			break;
		case VK_UP:
			controllerInput = XINPUT_GAMEPAD_DPAD_UP;
			break;
		case VK_DOWN:
			controllerInput = XINPUT_GAMEPAD_DPAD_DOWN;
			break;
		case VK_RETURN:
			controllerInput = XINPUT_GAMEPAD_A;
			break;
		case VK_ESCAPE:
			controllerInput = XINPUT_GAMEPAD_B;
			break;
		case VK_DELETE:
			controllerInput = XINPUT_GAMEPAD_X;
			break;
		default:
			break;
	}

	if (!controllerInput) return 0;
	if (controllerInput == XINPUT_GAMEPAD_A || controllerInput == XINPUT_GAMEPAD_B) {
		static CNyaTimer timer;
		if (fButtonRemapTimeout > 0) {
			fButtonRemapTimeout -= timer.Process();
			return 0;
		}
	}

	for (int i = 0; i < XUSER_MAX_COUNT; i++) {
		auto state = gPadState[i];
		auto lastState = gPadLastState[i];
		if ((state.Gamepad.wButtons & controllerInput) != 0 && (lastState.Gamepad.wButtons & controllerInput) == 0) return 1;
		if (inputId == VK_RETURN && (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0 && (lastState.Gamepad.wButtons & XINPUT_GAMEPAD_START) == 0) return 1;
	}
	return 0;
}

void __fastcall GetMenuInputsHooked(int inputId) {
	nMenuInputReturnValue = GetMenuInputs(inputId);
}

void __attribute__((naked)) MenuControllerSupportSetupASM() {
	__asm__ (
		"pushad\n\t"
		"mov ecx, eax\n\t"
		"call %0\n\t"
		"popad\n\t"
		"mov eax, %1\n\t"
		"ret\n\t"
			:
			:  "i" (GetMenuInputsHooked), "m" (nMenuInputReturnValue)
	);
}

auto sub_4AE5D0 = (void*(__stdcall*)(void*))0x4AE5D0;
void* __stdcall ControllerSupportRemapDisable(void* a1) {
	bControllerSupportDisabledForRemapping = true;
	return sub_4AE5D0(a1);
}

auto sub_4AE690 = (void*(__stdcall*)(void*))0x4AE690;
void* __stdcall ControllerSupportRemapReenable(void* a1) {
	bControllerSupportDisabledForRemapping = false;
	return sub_4AE690(a1);
}

auto sub_55A9F0 = (uint8_t(__thiscall*)(void*, int))0x55A9F0;
uint8_t __fastcall ControllerSupportIngamePause(void* a1, void*, int key) {
	if (key == 9) {
		for (int i = 0; i < XUSER_MAX_COUNT; i++) {
			auto state = gPadState[i];
			auto lastState = gPadLastState[i];
			if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0 && (lastState.Gamepad.wButtons & XINPUT_GAMEPAD_START) == 0) return 1;
		}
	}
	return sub_55A9F0(a1, key);
}

auto sub_55AA10 = (uint8_t(__thiscall*)(void*, int))0x55AA10;
uint8_t __fastcall ControllerSupportIngameMenu(void* a1, void*, int key) {
	if (auto value = sub_55AA10(a1, key)) return value;

	int controllerInput = 0;
	switch (key) {
		case INPUT_MENU_ACCEPT:
			controllerInput = XINPUT_GAMEPAD_A;
			break;
		case INPUT_PAUSE:
			controllerInput = XINPUT_GAMEPAD_B;
			break;
		case INPUT_ACCELERATE:
			controllerInput = XINPUT_GAMEPAD_DPAD_UP;
			break;
		case INPUT_BRAKE:
			controllerInput = XINPUT_GAMEPAD_DPAD_DOWN;
			break;
		case INPUT_STEER_LEFT:
			controllerInput = XINPUT_GAMEPAD_DPAD_LEFT;
			break;
		case INPUT_STEER_RIGHT:
			controllerInput = XINPUT_GAMEPAD_DPAD_RIGHT;
			break;
		default:
			break;
	}

	if (!controllerInput) return 0;

	for (int i = 0; i < XUSER_MAX_COUNT; i++) {
		auto state = gPadState[i];
		auto lastState = gPadLastState[i];
		if ((state.Gamepad.wButtons & controllerInput) != 0 && (lastState.Gamepad.wButtons & controllerInput) == 0) return 1;
	}
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD fdwReason, LPVOID) {
	switch( fdwReason ) {
		case DLL_PROCESS_ATTACH: {
			if (NyaHookLib::GetEntryPoint() != 0x202638) {
				MessageBoxA(nullptr, "Unsupported game version! Make sure you're using v1.2 (.exe size of 2990080 bytes)", "nya?!~", MB_ICONERROR);
				exit(0);
				return TRUE;
			}

			EndSceneOrig = (HRESULT(__thiscall *)(void *))(*(uintptr_t*)0x67D5A4);
			NyaHookLib::Patch(0x67D5A4, &EndSceneHook);
			NyaHookLib::PatchRelative(NyaHookLib::JMP, 0x5503F0, &MenuControllerSupportSetupASM);
			sub_4AE5D0 = (void*(__stdcall*)(void*))(*(uintptr_t*)(0x4A725E + 1));
			NyaHookLib::Patch(0x4A725E + 1, &ControllerSupportRemapDisable);
			sub_4AE690 = (void*(__stdcall*)(void*))(*(uintptr_t*)(0x4A729D + 1));
			NyaHookLib::Patch(0x4A729D + 1, &ControllerSupportRemapReenable);
			sub_55A9F0 = (uint8_t(__thiscall*)(void*, int))(*(uintptr_t*)0x67B840);
			NyaHookLib::Patch(0x67B840, &ControllerSupportIngamePause);
			sub_55AA10 = (uint8_t(__thiscall*)(void*, int))(*(uintptr_t*)0x67B83C);
			NyaHookLib::Patch(0x67B83C, &ControllerSupportIngameMenu);
		} break;
		default:
			break;
	}
	return TRUE;
}