#include <switch.h>
#include <stdio.h> //required for printf
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "ftp.h"
#include "gfx.h"
#include "cons.h"

/*
Information about button codes
"[A]", "\ue0e0"
"[B]", "\ue0e1"
"[X]", "\ue0e2"
"[Y]", "\ue0e3"
"[L]", "\ue0e4"
"[R]", "\ue0e5"
"[ZL]", "\ue0e6"
"[ZR]", "\ue0e7"
"[SL]", "\ue0e8"
"[SR]", "\ue0e9"
"[DPAD]", "\ue0ea"
"[DUP]", "\ue0eb"
"[DDOWN]", "\ue0ec"
"[DLEFT]", "\ue0ed"
"[DRIGHT]", "\ue0ee"
"[+]", "\ue0ef"
"[-]", "\ue0f0"

console colours
| = yellow (BGR-00FFFF)
^ = Green (BGR-FF00FF00)
& = Dark Yellow (BGR-009dff)
$ = Light pink (BGR-e266ff)
* = Faded red (BGR-3333FF)
@ = Purple (BGR-ff0059)
+ = Orange (BGR-0058FF)
~ = white
# = Custom colour
*/

console infoCons(28);
font* sysFont, * consFont;
const char* ctrlStr = "&\ue0f0& |Toggle FTP Server|   &\ue0e2& |Clear Console|   &\ue0ef& |Quit|";
const char* header = "&Switch& |FTP| &Server&"; //drawText last variable creates new hash colours

tex* top = texCreate(1280, 88);
tex* bot = texCreate(1280, 72);

uint32_t con_text_col = 0xFF4763FF; //set default console text colour ABGR
uint8_t con_text_size = 32; //set default console font size
uint32_t Hash_col = 0xFFD984A2; //define custom font colour between 2 hashes (ABGR)

PadState pad;
HidsysUniquePadId g_unique_pad_ids[2] = { 0 };
s32 g_total_entries = 0;
bool g_led_state = false; // Track current LED state

// FTP LED state management
static bool wasClientConnected = false;
static bool currentLedPatternSet = false;
extern bool start_logs;

uint32_t lastFtpToggleFrame = 0;
int g_max_clients_override = 1;
int connected_clients = 0;


enum LedState {
	LED_SOLID,
	LED_BLINK_SLOW,
	LED_PULSE_FAST,
	LED_BREATHING,
	LED_DOUBLE_BLINK
};

static std::string format(const char* fmt, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	return std::string(buffer);
}

void set_led_pattern_for_state(HidsysNotificationLedPattern& pattern, LedState state) {
	memset(&pattern, 0, sizeof(pattern));

	switch (state) {
	case LED_SOLID:
		// Solid pattern (FTP active)
		pattern.baseMiniCycleDuration = 0x1;
		pattern.totalMiniCycles = 0x1;
		pattern.totalFullCycles = 0x0;
		pattern.startIntensity = 0xF;
		pattern.miniCycles[0].ledIntensity = 0xF;
		pattern.miniCycles[0].transitionSteps = 0x0;
		pattern.miniCycles[0].finalStepDuration = 0x7;
		break;

	case LED_BLINK_SLOW:
		// Slow blink (Waiting for connection)
		pattern.baseMiniCycleDuration = 0x8;
		pattern.totalMiniCycles = 0x2;
		pattern.totalFullCycles = 0x0;
		pattern.miniCycles[0].ledIntensity = 0xF;
		pattern.miniCycles[0].transitionSteps = 0x0;
		pattern.miniCycles[0].finalStepDuration = 0xF;
		pattern.miniCycles[1].ledIntensity = 0x0;
		pattern.miniCycles[1].transitionSteps = 0x0;
		pattern.miniCycles[1].finalStepDuration = 0xF;
		break;

	case LED_PULSE_FAST:
		// Fast pulse (File transfer in progress)
		pattern.baseMiniCycleDuration = 0x2;
		pattern.totalMiniCycles = 0x3;
		pattern.totalFullCycles = 0x0;
		pattern.miniCycles[0].ledIntensity = 0xF;
		pattern.miniCycles[0].transitionSteps = 0x5;
		pattern.miniCycles[0].finalStepDuration = 0x2;
		pattern.miniCycles[1].ledIntensity = 0x0;
		pattern.miniCycles[1].transitionSteps = 0x5;
		pattern.miniCycles[1].finalStepDuration = 0x2;
		pattern.miniCycles[2].ledIntensity = 0x0;
		pattern.miniCycles[2].transitionSteps = 0x0;
		pattern.miniCycles[2].finalStepDuration = 0x3;
		break;

	case LED_BREATHING:
		pattern.baseMiniCycleDuration = 0x4;
		pattern.totalMiniCycles = 0x2;
		pattern.totalFullCycles = 0x0;
		pattern.miniCycles[0].ledIntensity = 0xF;
		pattern.miniCycles[0].transitionSteps = 0xA;
		pattern.miniCycles[0].finalStepDuration = 0x1;
		pattern.miniCycles[1].ledIntensity = 0x0;
		pattern.miniCycles[1].transitionSteps = 0xA;
		pattern.miniCycles[1].finalStepDuration = 0x1;
		break;

	case LED_DOUBLE_BLINK:
		pattern.baseMiniCycleDuration = 0x3;
		pattern.totalMiniCycles = 0x4;
		pattern.totalFullCycles = 0x3;
		pattern.miniCycles[0].ledIntensity = 0xF;
		pattern.miniCycles[0].transitionSteps = 0x0;
		pattern.miniCycles[0].finalStepDuration = 0x3;
		pattern.miniCycles[1].ledIntensity = 0x0;
		pattern.miniCycles[1].transitionSteps = 0x0;
		pattern.miniCycles[1].finalStepDuration = 0x2;
		pattern.miniCycles[2].ledIntensity = 0xF;
		pattern.miniCycles[2].transitionSteps = 0x0;
		pattern.miniCycles[2].finalStepDuration = 0x3;
		pattern.miniCycles[3].ledIntensity = 0x0;
		pattern.miniCycles[3].transitionSteps = 0x0;
		pattern.miniCycles[3].finalStepDuration = 0x8;
		break;
	}
}

void turn_led_on(LedState patternType = LED_SOLID) {
	HidsysNotificationLedPattern pattern;
	set_led_pattern_for_state(pattern, patternType);  // Pass the pattern by reference

	// Always refresh pad IDs to ensure they're current
	padUpdate(&pad);
	g_total_entries = 0;
	memset(g_unique_pad_ids, 0, sizeof(g_unique_pad_ids));

	HidNpadIdType npad_id_type = padIsHandheld(&pad) ? HidNpadIdType_Handheld : HidNpadIdType_No1;
	Result rc = hidsysGetUniquePadsFromNpad(npad_id_type, g_unique_pad_ids, 2, &g_total_entries);

	if (R_SUCCEEDED(rc) && g_total_entries > 0) {
		for (int i = 0; i < g_total_entries; i++) {
			hidsysSetNotificationLedPattern(&pattern, g_unique_pad_ids[i]);
		}
		g_led_state = true;
	}
	else {
		infoCons.out(format("Failed to set LED pattern. RC: 0x%x, Entries: %d\n", rc, g_total_entries));
		infoCons.nl();
	}
}

void turn_led_off() {
	HidsysNotificationLedPattern pattern;
	memset(&pattern, 0, sizeof(pattern)); // Zero pattern turns LED off

	// Always refresh pad IDs to ensure they're current
	padUpdate(&pad);
	g_total_entries = 0;
	memset(g_unique_pad_ids, 0, sizeof(g_unique_pad_ids));

	HidNpadIdType npad_id_type = padIsHandheld(&pad) ? HidNpadIdType_Handheld : HidNpadIdType_No1;
	Result rc = hidsysGetUniquePadsFromNpad(npad_id_type, g_unique_pad_ids, 2, &g_total_entries);

	if (R_SUCCEEDED(rc) && g_total_entries > 0) {
		for (int i = 0; i < g_total_entries; i++) {
			hidsysSetNotificationLedPattern(&pattern, g_unique_pad_ids[i]);
		}
		g_led_state = false;
	}
}

void toggle_led(LedState patternType = LED_SOLID) {
	if (g_led_state) {
		turn_led_off();
	}
	else {
		turn_led_on(patternType);
	}
}

bool print_ip_local() {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		infoCons.out("Cannot create socket");
		infoCons.nl();
		return false;
	}

	// Try connecting to local broadcast address (works even without internet)
	struct sockaddr_in remote = {};
	remote.sin_family = AF_INET;
	remote.sin_port = htons(9);
	remote.sin_addr.s_addr = inet_addr("255.255.255.255");

	// Enable broadcast
	int broadcast = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
		infoCons.out("Setsockopt failed");
		infoCons.nl();
		close(sock);
		return false;
	}

	if (connect(sock, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
		infoCons.out("Local network not available");
		infoCons.nl();
		close(sock);
		return false;
	}

	// Get the local address
	struct sockaddr_in local;
	socklen_t len = sizeof(local);
	if (getsockname(sock, (struct sockaddr*)&local, &len) == 0) {
		infoCons.out(format("Your Local IP Address: ~%s~", inet_ntoa(local.sin_addr)));
		infoCons.nl();
		close(sock);
		return true;
	}
	else {
		infoCons.out("Could not get IP address");
		infoCons.nl();
	}

	close(sock);
	return false;
}

int main(int argc, char* argv[]) {
	// Set media playback state to prevent switch sleeping
	appletSetMediaPlaybackState(true);

	// Toggle FTP Server Button vars
	bool running = true;

	// Enable gamepad
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	hidsysInitialize();

	graphicsInit(1280, 720);
	romfsInit();
	//make sure to load AFTER romfsInit();
	tex* bgbottom = texLoadPNGFile("romfs:/bar.png");
	tex* bg = texLoadJPEGFile("romfs:/splash.jpg");
	tex* icon_red = texLoadPNGFile("romfs:/icons/red.png");
	tex* icon_green = texLoadPNGFile("romfs:/icons/green.png");
	tex* icon_white = texLoadPNGFile("romfs:/icons/white.png");
	tex* icon_clear = texLoadPNGFile("romfs:/icons/clear.png");
	tex* icon;

	bool network_available = false;

	// Test if wifi is connected and show the IP address
	if (R_FAILED(socketInitializeDefault())) {
		infoCons.out("Network unavailable");
		infoCons.nl();
		icon = icon_red;
	}
	else {
		network_available = print_ip_local();
		socketExit();
	}

	// Execute appropriate function based on network status
	if (network_available) {
		// Just show message that FTP can be started - don't initialize yet
		infoCons.out("FTP server ready - Press &Minus& to start/stop");
		infoCons.nl();
		icon = icon_white;
	}
	else {
		icon = icon_red;
	}

	// Add FTP state variables
	bool ftpEnabled = false;
	bool ftpInitialized = false; // Track if FTP has been initialized
	int frameCount = 0;

	// Add delay to ensure console update is completed
	//svcSleepThread(100'000'000); // 100ms delay

	sysFont = fontLoadSharedFonts();
	consFont = fontLoadTTF("romfs:/clacon.ttf");

	unsigned ctrlX = 30;
	unsigned cent = 30;

	infoCons.setStartPos(110); //insfobox text start at position from top of screen
	infoCons.setEndPos(654); //infobox text last position from top of screen
	infoCons.setLeftPos(30); //infobox text start at pixels left of screen

	//Top
	//texClearColor(top, clrCreateU32(0xFF0f0707)); //top block
	drawTextWrap(header, top, sysFont, 506, 26, 24, clrCreateU32(0xFFf2bfbf), 1280, 0xFF0073ff);
	//drawRect(top, 0, 87, 1280, 1, clrCreateU32(0xffe5e5e5)); //1 pixel grey line

	//Bot
	//texClearColor(bot, clrCreateU32(0xFF0f0707)); //bottom block
	//drawRect(bot, 0, 0, 1280, 1, clrCreateU32(0xffe5e5e5)); //1 pixel grey line
	drawText(ctrlStr, bot, sysFont, ctrlX, cent-2, 18, clrCreateU32(0xFFFFFFFF), 0xFFFFFFFF);

	while (running) {
		padUpdate(&pad);
		u64 kDown = padGetButtonsDown(&pad);
		frameCount++;

		if (kDown & HidNpadButton_Plus) running = false;

		if (network_available) {
			// Handle max clients override BEFORE FTP is initialized or enabled
			if ((kDown & HidNpadButton_Up) && (!ftpEnabled)) {
				if (g_max_clients_override < 3) {
					g_max_clients_override++;
					infoCons.out(format("Max clients override: &%d&", g_max_clients_override));
					infoCons.nl();
				}
			}

			if ((kDown & HidNpadButton_Down) && (!ftpEnabled)) {
				if (g_max_clients_override > 1) {
					g_max_clients_override--;
					infoCons.out(format("Max clients override: &%d&", g_max_clients_override));
					infoCons.nl();
				}
			}

			// Optional: Reset override with Left button
			if ((kDown & HidNpadButton_Left) && (!ftpEnabled)) {
				g_max_clients_override = 1;
				infoCons.out(format("Max clients override &reset&"));
				infoCons.nl();
			}

			if (kDown & HidNpadButton_X) {
				infoCons.clear();
			}

			if ((kDown & HidNpadButton_Y) && (frameCount - lastFtpToggleFrame > 30)) {
				infoCons.clear();
				start_logs = !start_logs;
				lastFtpToggleFrame = frameCount;
				if (start_logs) {
					infoCons.out("Logging Enabled");
					infoCons.nl();
				}
				else {
					infoCons.out("Logging Disabled");
					infoCons.nl();
				}
			}

			// FTP server toggle with Minus button (with debouncing)
			if ((kDown & HidNpadButton_Minus) && (frameCount - lastFtpToggleFrame > 30)) {
				ftpEnabled = !ftpEnabled;
				lastFtpToggleFrame = frameCount;

				if (ftpEnabled) {
					// Initialize FTP here, AFTER user has had chance to set max clients override
					if (!ftpInitialized) {
						if (!ftp_init()) {
							infoCons.out("Failed to initialize FTP server");
							infoCons.nl();
							icon = icon_red;
							ftpEnabled = false;
							continue; // Skip the rest of this iteration
						}
						ftpInitialized = true;
					}

					ftp_start(&pad);
					infoCons.out("FTP server: &Started& and waiting for a client");
					infoCons.nl();
					turn_led_on(LED_BREATHING);  // Start with breathing pattern
					wasClientConnected = false;   // Reset connection state
					currentLedPatternSet = true;
					ftpEnabled = true;
					icon = icon_clear;
				}
				else {
					ftp_stop(&pad);   // Pass the main program's pad
					ftpInitialized = false;
					infoCons.out("FTP server: &Stopped&");
					infoCons.nl();
					turn_led_off();
					ftpEnabled = false;
					// Note: We don't reinitialize here - keep the configuration for next start
					//ftp_init(); //reinitialse as we cleaned the socket...
					icon = icon_white;
				}
			}

			// Update FTP server if running
			if (ftp_is_running()) {
				ftp_update();
			}

			if (ftpEnabled && ftp_is_running()) {
				bool isClientConnected = user_connected();

				// Only change LED pattern when connection state changes
				if (isClientConnected != wasClientConnected) {
					if (isClientConnected) {
						// Client just connected - set solid pattern
						turn_led_on(LED_SOLID);
						//infoCons.out("FTP client: #Connected#");
						//infoCons.nl();
						icon = icon_green;
					}
					else {
						if (connected_clients <= 0) {
							// Client just disconnected - set breathing pattern
							turn_led_on(LED_BREATHING);
							//infoCons.out("FTP client: #Disconnected#");
							//infoCons.nl();
							icon = icon_clear;
						}
					}
					wasClientConnected = isClientConnected;
					currentLedPatternSet = true;
				}
			}

			else if (currentLedPatternSet) {
				// FTP is not running, ensure LED is off and reset state
				if (g_led_state) {
					turn_led_off();
				}
				wasClientConnected = false;
				currentLedPatternSet = false;
			}
		}

		gfxBeginFrame();
		texClearColor(frameBuffer, clrCreateU32(con_text_col));
		texClearColor(frameBuffer, clrCreateU32(Hash_col));
		if (bg != NULL)
		{
			texDraw(bg, frameBuffer, 0, 0); // Draw background covering entire screen
		}
		texDraw(top, frameBuffer, 0, 0);
		texDraw(bot, frameBuffer, 0, 648);
		infoCons.draw(consFont, con_text_size, con_text_col, 1250, Hash_col);
		if (icon != NULL) {
			texDraw(icon, frameBuffer, 1230, 667);
		}
		if (bgbottom != NULL)
		{
			texDraw(bgbottom, frameBuffer, 0, 658); // Draw background covering entire screen
		}
		gfxEndFrame();

	}

	// Cleanup
	if (g_led_state) {
		turn_led_off();
	}
	appletSetMediaPlaybackState(false);

	// Stop services
	if (ftpInitialized) {
		ftp_cleanup(&pad);
	}

	hidsysExit();
	socketExit();

	// Destroy fonts
	if (sysFont) fontDestroy(sysFont);
	if (consFont) fontDestroy(consFont);

	// Destroy textures
	if (top) texDestroy(top);
	if (bot) texDestroy(bot);
	if (bg) texDestroy(bg);
	if (bgbottom) texDestroy(bgbottom);
	if (icon_red) texDestroy(icon_red);
	if (icon_green) texDestroy(icon_green);
	if (icon_white) texDestroy(icon_white);
	if (icon_clear) texDestroy(icon_clear);

	graphicsExit();
	romfsExit();
	hidExit();

	return 0;
}
