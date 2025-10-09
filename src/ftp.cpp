/*
Nintendo Switch FTP Plugin
Created By MrDude
*/

#include <switch.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/types.h>
#include <errno.h>
#include <string>
#include <sys/stat.h> // For stat(), S_ISDIR, S_ISREG
#include <netinet/in.h>
#include <fcntl.h>
#include <chrono>
#include "ftp.h"
#include "../inc/cons.h"

// Add these after the includes
extern console infoCons;  // Declare the external console

const char* conf_dir = "sdmc:/switch/FTP-Server";
std::string conf_path = std::string(conf_dir) + "/ftp_config.txt";
const char* config_path = conf_path.c_str();

// === Configuration Structure ===
typedef struct {
	int ftp_port;
	char username[32];
	char password[32];
	int max_clients;
	bool logging_enabled;
} FTPConfig;

// Default configuration (current hardcoded values)
static FTPConfig g_ftp_config = {
	.ftp_port = 5000,
	.username = "switch",
	.password = "ftp123",
	.max_clients = 1,
	.logging_enabled = false
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

// Helper function to format file sizes nicely (KB, MB, etc.)
std::string format_size(long bytes) {
	const char* sizes[] = { "B", "KB", "MB", "GB" };
	int order = 0;
	double size = (double)bytes;

	while (size >= 1024.0 && order < 3) {
		order++;
		size /= 1024.0;
	}

	char buffer[32];
	if (order == 0) {
		snprintf(buffer, sizeof(buffer), "%ld %s", bytes, sizes[order]);
	}
	else {
		snprintf(buffer, sizeof(buffer), "%.1f %s", size, sizes[order]);
	}

	return std::string(buffer);
}

// Configuration function declarations
bool load_ftp_config(void);
void create_default_config(void);
int get_ftp_port(void);
const char* get_ftp_username(void);
const char* get_ftp_password(void);
int get_max_clients(void);
bool get_logging_enabled(void);

// Now define the LOG macro AFTER the configuration functions are declared
// Macro to simplify logging (automatically captures __FILE__ and __LINE__)
#define LOG(level, ...) write_log_advanced(level, __FILE__, __LINE__, __VA_ARGS__)

// Log levels
typedef enum {
	LOG_INFO,
	LOG_WARNING,
	LOG_ERROR
} LogLevel;

// Get log level as a string
const char* get_log_level_str(LogLevel level) {
	switch (level) {
	case LOG_INFO:    return "INFO";
	case LOG_WARNING: return "WARN";
	case LOG_ERROR:   return "ERROR";
	default:          return "UNKNOWN";
	}
}

// === Configuration File Parser ===
bool load_ftp_config(void) {
	FILE* file = fopen(config_path, "r");

	if (!file) {
		// Use printf instead of LOG since LOG might not be ready yet
		//printf("No config file found, using defaults\n");
		if (g_max_clients_override > 1) {
			g_ftp_config.max_clients = g_max_clients_override;
		}
		return false;
	}

	char line[256];
	int line_num = 0;

	while (fgets(line, sizeof(line), file)) {
		line_num++;

		// Skip empty lines and comments
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
			continue;
		}

		// Remove trailing newline
		line[strcspn(line, "\r\n")] = 0;

		// Parse key-value pairs
		char key[64], value[128];
		if (sscanf(line, "%63[^=]=%127[^\n]", key, value) == 2) {
			// Trim whitespace from value
			char* trimmed_value = value;
			while (*trimmed_value == ' ') trimmed_value++;

			if (strcmp(key, "port") == 0) {
				int port = atoi(trimmed_value);
				if (port > 0 && port <= 65535) {
					g_ftp_config.ftp_port = port;
					//printf("Config: Port set to %d\n", port);
				}
			}
			else if (strcmp(key, "username") == 0) {
				strncpy(g_ftp_config.username, trimmed_value, sizeof(g_ftp_config.username) - 1);
				g_ftp_config.username[sizeof(g_ftp_config.username) - 1] = '\0';
				//printf("Config: Username set to %s\n", g_ftp_config.username);
			}
			else if (strcmp(key, "password") == 0) {
				strncpy(g_ftp_config.password, trimmed_value, sizeof(g_ftp_config.password) - 1);
				g_ftp_config.password[sizeof(g_ftp_config.password) - 1] = '\0';
				//printf("Config: Password set\n");
			}
			else if (strcmp(key, "max_clients") == 0) {
				int max_clients = atoi(trimmed_value);
				if (max_clients > 0 && max_clients <= 3) { // Reasonable limit
					if (g_max_clients_override > 1) {
						max_clients = g_max_clients_override;
					}
					g_ftp_config.max_clients = max_clients;
				}
			}
			/*
			else if (strcmp(key, "logging") == 0) {
				if (strcmp(trimmed_value, "true") == 0 || strcmp(trimmed_value, "1") == 0) {
					g_ftp_config.logging_enabled = true;
					//printf("Config: Logging enabled\n");
				}
				else {
					g_ftp_config.logging_enabled = false;
					//printf("Config: Logging disabled\n");
				}
			}
			*/
		}
	}

	fclose(file);
	//printf("FTP configuration loaded successfully\n");
	return true;
}

// === Create default config file ===
void create_default_config(void) {
	struct stat st = { 0 };
	// Create the /logs directory if it doesn't exist
	if (stat(conf_dir, &st) == -1) {
		mkdir(conf_dir, 0777); // 0777 = Full permissions
	}


	FILE* file = fopen(config_path, "w");

	if (!file) {
		//printf("Failed to create default config file\n");
		return;
	}

	fprintf(file, "# FTP Server Configuration\n");
	fprintf(file, "# Created automatically - modify as needed\n\n");

	fprintf(file, "# FTP port (1-65535)\n");
	fprintf(file, "port=%d\n\n", g_ftp_config.ftp_port);

	fprintf(file, "# Login username\n");
	fprintf(file, "username=%s\n\n", g_ftp_config.username);

	fprintf(file, "# Login password\n");
	fprintf(file, "password=%s\n\n", g_ftp_config.password);

	fprintf(file, "# Maximum simultaneous clients (1-3)\n");
	fprintf(file, "max_clients=%d\n\n", g_ftp_config.max_clients);

	/*
	fprintf(file, "# Enable file logging (true/false)\n");
	fprintf(file, "logging=%s\n", g_ftp_config.logging_enabled ? "true" : "false");
	*/

	fclose(file);
	//printf("Default config file created: %s\n", config_path);
}

// Configuration getter functions
int get_ftp_port(void) { return g_ftp_config.ftp_port; }
const char* get_ftp_username(void) { return g_ftp_config.username; }
const char* get_ftp_password(void) { return g_ftp_config.password; }
int get_max_clients(void) { return g_ftp_config.max_clients; }
bool get_logging_enabled(void) { return g_ftp_config.logging_enabled; }

#define BUFFER_SIZE 4096
int control_sock = 0;
bool startftp = true;
bool listening = true;
bool network_available = false;
bool client_connected = false;
bool start_logs = false;

// MOTD (message of the day) load from text file
#define MOTD_FILE "sdmc:/switch/ftp-plugin/ftp_motd.txt"
// MOTD as a const char*
const char* MOTD =
"230 Welcome to your Nintendo Switch FTP Server!\r\n"
"230 MOTD: Written by MrDude!\r\n";

// Client tracking for cleanup
static ClientState** g_active_clients = NULL;
static bool* g_thread_active = NULL;
static Thread* g_client_threads = NULL;
static Mutex g_clients_mutex;
static bool g_server_running = true;
// Remove this line: static Thread g_client_threads[MAX_CLIENTS];

// FTP server control variables
static PadState g_ftp_pad;
static bool g_ftp_running = false;

int get_file_type(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0) {
		return -1;  // Error: Path does not exist or is inaccessible
	}

	if (S_ISDIR(st.st_mode)) {
		return 1;   // It's a directory
	}

	if (S_ISREG(st.st_mode)) {
		return 0;   // It's a file
	}

	return -2; // Unknown type (not a file or directory)
}

static void add_client(ClientState* client) {
	mutexLock(&g_clients_mutex);
	int max_clients = get_max_clients();
	for (int i = 0; i < max_clients; i++) {
		if (g_active_clients[i] == NULL) {
			g_active_clients[i] = client;
			break;
		}
	}
	mutexUnlock(&g_clients_mutex);
}

static void remove_client(ClientState* client) {
	mutexLock(&g_clients_mutex);
	int max_clients = get_max_clients();
	for (int i = 0; i < max_clients; i++) {
		if (g_active_clients[i] == client) {
			g_active_clients[i] = NULL;
			break;
		}
	}
	mutexUnlock(&g_clients_mutex);
}

// Enhanced logging function with format support
bool write_log_advanced(LogLevel level, const char* file, int line, const char* format, ...) {
	if (get_logging_enabled()) {
		const char* log_dir = "sdmc:/ftp-logs";
		struct stat st = { 0 };
		// Create the /logs directory if it doesn't exist
		if (stat(log_dir, &st) == -1) {
			mkdir(log_dir, 0777); // 0777 = Full permissions
		}

		const char* log_path = "sdmc:/ftp-logs/log.txt";
		FILE* file_ptr = fopen(log_path, "a");
		if (!file_ptr) return false;

		// Timestamp
		time_t now = time(NULL);
		struct tm* timeinfo = localtime(&now);
		char timestamp[20];
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

		// Format the message
		char message[256];
		va_list args;
		va_start(args, format);
		vsnprintf(message, sizeof(message), format, args);
		va_end(args);

		// Write to file
		fprintf(file_ptr, "%s", message);
		fclose(file_ptr);
	}
	return true;
}
// Helper functions for path handling
void normalize_path(char* path) {
	char* src = path;
	char* dest = path;
	int last_was_slash = 0;

	while (*src) {
		if (*src == '/') {
			if (!last_was_slash) {
				*dest++ = *src;
			}
			last_was_slash = 1;
		}
		else {
			*dest++ = *src;
			last_was_slash = 0;
		}
		src++;
	}
	*dest = '\0';

	// Ensure at least "/" remains
	if (dest == path) {
		*dest++ = '/';
	}
	*dest = '\0';
}
// Helper function for safe path handling
void build_safe_path(char* dest, size_t dest_size, const char* dir, const char* file) {
	size_t dir_len = strnlen(dir, dest_size);
	size_t file_len = strnlen(file, dest_size);

	// Calculate available space for each component
	size_t max_dir = dest_size - file_len - 2; // Reserve space for / and null
	size_t max_file = dest_size - dir_len - 2;

	snprintf(dest, dest_size, "%.*s/%.*s",
		(int)(dir_len < max_dir ? dir_len : max_dir), dir,
		(int)(file_len < max_file ? file_len : max_file), file);
}
// Helper function for safe path joining
void safe_path_join(char* dest, size_t size, const char* base, const char* part) {
	if (!base || !part || size == 0) {
		strncpy(dest, "/", size);
		return;
	}

	// If `part` is already an absolute path, use it directly
	if (part[0] == '/') {
		strncpy(dest, part, size - 1);
	}
	else {
		// Otherwise, concatenate `base` + `part`
		snprintf(dest, size, "%s/%s", base, part);
	}

	dest[size - 1] = '\0';  // Ensure null termination

	// Normalize path (remove double slashes)
	char* p = dest;
	while (*p) {
		if (*p == '/' && *(p + 1) == '/') {
			memmove(p, p + 1, strlen(p));  // Shift string left to remove extra slash
		}
		else {
			p++;
		}
	}

	// Create clean version of part for logging
	char clean_part[256];
	size_t i = 0;
	const char* src = part;
	while (*src && i < sizeof(clean_part) - 1) {
		if (*src != '\n' && *src != '\r') {
			clean_part[i++] = *src;
		}
		if (*src != '\n') {
			clean_part[i++] = *src;
		}
		src++;
	}
	clean_part[i] = '\0';

	LOG(LOG_INFO, "[DEBUG] safe_path_join: base=%s part=%s -> dest=%s\n", base, part, dest);
}
// Modified send_response to handle formatted strings
void send_response(int sock, const char* format, ...) {
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	send(sock, buffer, strlen(buffer), 0);
}
int recursive_delete(const char* path) {
	if (path == NULL || strlen(path) == 0) {
		LOG(LOG_ERROR, "[DEBUG] Path is NULL or empty\n");
		return -1;
	}

	LOG(LOG_INFO, "[DEBUG] Checking path: %s\n", path);

	struct stat st;
	if (stat(path, &st) != 0) {
		LOG(LOG_ERROR, "[DEBUG] Path does not exist: %s (errno: %d)\n", path, errno);
		return -1;  // Path doesn't exist
	}

	if (S_ISDIR(st.st_mode)) {
		DIR* dir = opendir(path);
		if (!dir) {
			LOG(LOG_ERROR, "[DEBUG] Failed to open directory: %s\n", path);
			return -1;
		}

		struct dirent* ent;
		char full_path[PATH_MAX];

		while ((ent = readdir(dir)) != NULL) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

			snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
			full_path[PATH_MAX - 1] = '\0';  // Ensure null termination

			LOG(LOG_INFO, "[DEBUG] Recursive Delete - Deleting: %s\n", full_path);

			// Recursively delete subdirectories and files
			if (recursive_delete(full_path) != 0) {
				closedir(dir);
				return -1;
			}
		}
		closedir(dir);

		LOG(LOG_INFO, "[DEBUG] Removing directory: %s\n", path);
		return rmdir(path);  // Remove empty directory
	}
	else {
		LOG(LOG_INFO, "[DEBUG] Removing file: %s\n", path);
		return unlink(path);  // Remove file
	}
}
int recursive_move(const char* src, const char* dest) {
	struct stat st;
	if (stat(src, &st) != 0) return -1;  // Source does not exist

	if (S_ISDIR(st.st_mode)) {
		// Create destination folder
		mkdir(dest, 0777);

		DIR* dir = opendir(src);
		if (!dir) return -1;

		struct dirent* ent;
		char src_path[PATH_MAX];
		char dest_path[PATH_MAX];

		while ((ent = readdir(dir)) != NULL) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

			snprintf(src_path, sizeof(src_path), "%s/%s", src, ent->d_name);
			snprintf(dest_path, sizeof(dest_path), "%s/%s", dest, ent->d_name);

			// Move files and subdirectories recursively
			if (recursive_move(src_path, dest_path) != 0) {
				closedir(dir);
				return -1;
			}
		}

		closedir(dir);
		return rmdir(src);  // Remove the empty source folder after moving
	}
	else {
		// Try rename first
		if (rename(src, dest) == 0) {
			return 0; // Success
		}

		// If rename fails, perform manual copy + delete
		FILE* src_file = fopen(src, "rb");
		if (!src_file) return -1;

		FILE* dest_file = fopen(dest, "wb");
		if (!dest_file) {
			fclose(src_file);
			return -1;
		}

		char buffer[4096];
		size_t bytes;
		while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
			fwrite(buffer, 1, bytes, dest_file);
		}

		fclose(src_file);
		fclose(dest_file);

		// Delete original file
		unlink(src);
		return 0;
	}
}
void handle_cdup(ClientState* state) {
	char original_dir[PATH_MAX];
	strncpy(original_dir, state->current_dir, PATH_MAX);

	if (strcmp(state->current_dir, "/") == 0) {
		send_response(state->client_sock, "550 Already at root directory\r\n");
		return;
	}

	char* last_slash = strrchr(state->current_dir, '/');
	if (last_slash) {
		if (last_slash == state->current_dir) {
			state->current_dir[1] = '\0';
		}
		else {
			*last_slash = '\0';
			if (strlen(state->current_dir) == 0) {
				strcpy(state->current_dir, "/");
			}
		}
		normalize_path(state->current_dir);

		DIR* dir = opendir(state->current_dir);
		if (dir) {
			closedir(dir);
			send_response(state->client_sock, "250 Directory changed to \"%s\"\r\n", state->current_dir);
		}
		else {
			strncpy(state->current_dir, original_dir, PATH_MAX);
			send_response(state->client_sock, "550 Directory not found\r\n");
		}
	}
	else {
		send_response(state->client_sock, "550 Invalid directory\r\n");
	}
}
void handle_chmod(ClientState* state, const char* cmd) {
	char filename[PATH_MAX] = { 0 };
	char mode_str[5] = { 0 };
	int mode = 0;

	if (sscanf(cmd, "SITE CHMOD %4s %255s", mode_str, filename) != 2) {
		send_response(state->client_sock, "501 Invalid syntax\r\n");
		return;
	}

	// Validate mode is octal
	for (int i = 0; mode_str[i]; i++) {
		if (!isdigit(mode_str[i]) || mode_str[i] > '7') {
			send_response(state->client_sock, "501 Invalid mode\r\n");
			return;
		}
	}
	mode = strtol(mode_str, NULL, 8);

	if (strcmp(mode_str, "7777") == 0) {
		LOG(LOG_INFO, "[DEBUG] Emergency\n");
		send_response(state->client_sock, "550 Emergency Mode Activated\r\n");
	}

	char path[PATH_MAX];
	safe_path_join(path, sizeof(path), state->current_dir, filename);

	struct stat st;
	if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
		send_response(state->client_sock, "550 Invalid target for CHMOD\r\n");
		return;
	}

	if (chmod(path, mode) == 0) {
		send_response(state->client_sock, "200 Permissions changed\r\n");
	}
	else {
		send_response(state->client_sock, "550 Permission change failed\r\n");
	}
}
void handle_cwd(ClientState* state, const char* new_dir) {
	LOG(LOG_INFO, "[DEBUG] Working Dir: %s\n", new_dir);

	char resolved_path[PATH_MAX];

	// If new_dir is absolute, use it directly; otherwise, append to current directory
	if (new_dir[0] == '/') {
		strncpy(resolved_path, new_dir, PATH_MAX - 1);
		resolved_path[PATH_MAX - 1] = '\0';  // Ensure null termination
	}
	else {
		safe_path_join(resolved_path, sizeof(resolved_path), state->current_dir, new_dir);
		resolved_path[PATH_MAX - 1] = '\0';  // Ensure null termination
	}

	// Normalize the path (resolve "..", remove duplicate slashes)
	normalize_path(resolved_path);
	LOG(LOG_INFO, "[DEBUG] Normalized directory path: %s\n", resolved_path);

	// Try to open the directory to check if it exists
	DIR* test_dir = opendir(resolved_path);
	if (test_dir) {
		closedir(test_dir);
		strncpy(state->current_dir, resolved_path, PATH_MAX);
		send_response(state->client_sock, "250 Directory successfully changed to \"%s\"\r\n", state->current_dir);
		LOG(LOG_INFO, "[DEBUG] Directory changed successfully: %s\n", state->current_dir);
	}
	else {
		perror("[ERROR] Failed to change directory");
		send_response(state->client_sock, "550 Failed to change directory\r\n");
	}
}
void handle_dele(ClientState* state, const char* filename) {
	char path[PATH_MAX];
	LOG(LOG_ERROR, "[DEBUG] Handle Delete - filename: %s\n", filename);
	safe_path_join(path, sizeof(path), state->current_dir, filename);
	LOG(LOG_ERROR, "[DEBUG] Handle Delete - path: %s", path);

	int type = get_file_type(path);
	if (type == 1) {
		send_response(state->client_sock, "550 Cannot delete a directory, use RMD instead\r\n");
		return;
	}

	if (type == 0 && remove(path) == 0) {
		send_response(state->client_sock, "250 File deleted\r\n");
	}
	else {
		send_response(state->client_sock, "550 Delete failed\r\n");
	}
}
void handle_list(ClientState* state) {

	// Open data connection
	if (state->using_active_mode) {
		LOG(LOG_INFO, "[DEBUG] Active mode: Connecting to client at %s:%d\n",
			inet_ntoa(state->client_data_addr.sin_addr),
			ntohs(state->client_data_addr.sin_port));

		state->data_client_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (connect(state->data_client_sock,
			(struct sockaddr*)&state->client_data_addr,
			sizeof(state->client_data_addr)) < 0) {
			send_response(state->client_sock, "425 Can't establish connection\r\n");
			perror("[ERROR] Failed to connect to client for active mode");
			return;
		}
	}
	else {
		LOG(LOG_INFO, "[DEBUG] Passive mode: Waiting for client to connect...\n");

		struct sockaddr_in data_client_addr;
		socklen_t data_client_len = sizeof(data_client_addr);
		state->data_client_sock = accept(state->data_sock,
			(struct sockaddr*)&data_client_addr,
			&data_client_len);
		if (state->data_client_sock < 0) {
			send_response(state->client_sock, "425 Can't open data connection\r\n");
			LOG(LOG_ERROR, "[ERROR] Failed to accept data connection");
			return;
		}
	}

	send_response(state->client_sock, "150 Opening data connection\r\n");

	// Open directory
	LOG(LOG_INFO, "[DEBUG] Opening directory: %s\n", state->current_dir);

	DIR* dir = opendir(state->current_dir);
	if (!dir) {
		send_response(state->client_sock, "550 Failed to open directory\r\n");
		close(state->data_client_sock);
		return;
	}

	struct dirent* ent;
	char line[512];
	char time_buf[32];

	// Send directory listing
	int sent_files = 0;

	while ((ent = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		size_t base_len = strnlen(state->current_dir, PATH_MAX);
		size_t name_len = strnlen(ent->d_name, NAME_MAX);

		if (base_len + name_len + 2 > PATH_MAX) continue;

		snprintf(path, sizeof(path), "%.*s/%.*s",
			(int)base_len, state->current_dir,
			(int)name_len, ent->d_name);

		struct stat st;
		if (stat(path, &st) != 0) continue;

		const char* type = S_ISDIR(st.st_mode) ? "d" : "-";
		struct tm tm;
		localtime_r(&st.st_mtime, &tm);
		strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", &tm);

		char safe_name[NAME_MAX + 1];
		size_t name_copy_len = strnlen(ent->d_name, sizeof(safe_name) - 1);
		memcpy(safe_name, ent->d_name, name_copy_len);
		safe_name[name_copy_len] = '\0';

		int len = snprintf(line, sizeof(line),
			"%srw-rw-r-- 1 switch switch %10ld %s %.*s\r\n",
			type, st.st_size, time_buf,
			(int)(sizeof(line) - 50), safe_name);

		if (len > 0 && (size_t)len < sizeof(line)) {
			send(state->data_client_sock, line, len, 0);
		}
		sent_files++;
	}

	closedir(dir);

	// Ensure all data is flushed and properly closed
	shutdown(state->data_client_sock, SHUT_WR);  // Mark as finished sending
	close(state->data_client_sock);
	state->data_client_sock = -1;

	if (sent_files > 0) {
		send_response(state->client_sock, "226 Transfer complete\r\n");
		LOG(LOG_INFO, "[DEBUG] Sent %d files.\n", sent_files);
	}
	else {
		send_response(state->client_sock, "226 No files found.\r\n");
		LOG(LOG_INFO, "[DEBUG] Directory is empty.\n");
	}
}
void handle_mkd(ClientState* state, const char* folder_name) {
	LOG(LOG_INFO, "[DEBUG] Request to create directory: %s", folder_name);

	char full_path[PATH_MAX];

	// Construct the full path inside "sdmc:/"
	safe_path_join(full_path, sizeof(full_path), state->current_dir, folder_name);
	full_path[PATH_MAX - 1] = '\0';  // Ensure null termination

	LOG(LOG_INFO, "[DEBUG] Full path for MKD: %s", full_path);

	// Strip trailing \r and \n from the path
	size_t len = strlen(full_path);
	while (len > 0 && (full_path[len - 1] == '\r' || full_path[len - 1] == '\n')) {
		full_path[len - 1] = '\0';
		len--;
	}

	LOG(LOG_INFO, "[DEBUG] Cleaned full path for MKD: %s\n", full_path);

	// Try creating the directory
	if (mkdir(full_path, 0777) == 0) {
		send_response(state->client_sock, "257 \"%s\" directory created\r\n", folder_name);
		LOG(LOG_INFO, "[DEBUG] Directory created successfully: %s\n", full_path);
	}
	else {
		LOG(LOG_ERROR, "[ERROR] mkdir() failed: %s (errno: %d)\n", strerror(errno), errno);
		send_response(state->client_sock, "550 Failed to create directory\r\n");
	}
}
void handle_mlsd(ClientState* state) {
	if (state->data_sock < 0) {
		send_response(state->client_sock, "425 No data connection\r\n");
		return;
	}

	send_response(state->client_sock, "150 Opening data connection for MLSD\r\n");
	LOG(LOG_INFO, "[DEBUG] MLSD listing: %s\n", state->current_dir);

	DIR* dir = opendir(state->current_dir);
	if (!dir) {
		LOG(LOG_ERROR, "[ERROR] Failed to open directory\n");
		send_response(state->client_sock, "550 Failed to open directory\r\n");
		return;
	}

	struct dirent* ent;
	struct stat st;
	char line[512];
	int total_files = 0;

	while ((ent = readdir(dir)) != NULL) {
		// Skip "." and ".."
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

		char full_path[PATH_MAX];
		safe_path_join(full_path, sizeof(full_path), state->current_dir, ent->d_name);

		if (stat(full_path, &st) == 0) {
			const char* type = S_ISDIR(st.st_mode) ? "type=dir;" : "type=file;";
			snprintf(line, sizeof(line), "%s size=%ld; %s\r\n", type, st.st_size, ent->d_name);

			send(state->data_client_sock, line, strlen(line), 0);
			LOG(LOG_INFO, "[DEBUG] MLSD Sent: %s\n", line);
			total_files++;
		}
	}

	closedir(dir);
	shutdown(state->data_client_sock, SHUT_WR);
	close(state->data_client_sock);
	state->data_client_sock = -1;

	close(state->data_sock);
	state->data_sock = -1;

	LOG(LOG_INFO, "[DEBUG] MLSD total files sent: %d\n", total_files);
	send_response(state->client_sock, "226 MLSD Transfer complete\r\n");
}
void handle_motd(char* buffer, size_t max_size) {
	FILE* file = fopen(MOTD_FILE, "r");
	if (file) {
		size_t read = fread(buffer, 1, max_size - 1, file);
		buffer[read] = '\0';
		fclose(file);
	}
	else {
		strcpy(buffer, MOTD); // Fallback to default
	}
}
void handle_mv(ClientState* state, const char* cmd) {
	char src[PATH_MAX], dest[PATH_MAX];

	// Extract filenames with spaces
	if (sscanf(cmd, "MV \"%[^\"]\" \"%[^\"]\"", src, dest) != 2) {
		if (sscanf(cmd, "MV %s %s", src, dest) != 2) {
			send_response(state->client_sock, "501 Syntax error in parameters\r\n");
			return;
		}
	}

	char full_src[PATH_MAX], full_dest[PATH_MAX];
	safe_path_join(full_src, sizeof(full_src), state->current_dir, src);
	safe_path_join(full_dest, sizeof(full_dest), state->current_dir, dest);

	LOG(LOG_INFO, "[DEBUG] Moving from: %s to %s\n", full_src, full_dest);

	int type = get_file_type(full_src);
	if (type == -1) {
		send_response(state->client_sock, "550 Source path does not exist\r\n");
		return;
	}

	if (recursive_move(full_src, full_dest) == 0) {
		send_response(state->client_sock, "250 Move successful\r\n");
	}
	else {
		send_response(state->client_sock, "550 Move failed\r\n");
	}
}
void handle_nlst(ClientState* state) {
	if (state->data_sock < 0) {
		send_response(state->client_sock, "425 No data connection\r\n");
		return;
	}

	send_response(state->client_sock, "150 Opening data connection for NLST\r\n");
	LOG(LOG_INFO, "[DEBUG] NLST listing: %s\n", state->current_dir);

	DIR* dir = opendir(state->current_dir);
	if (!dir) {
		perror("[ERROR] Failed to open directory");
		send_response(state->client_sock, "550 Failed to open directory\r\n");
		return;
	}

	struct dirent* ent;
	int total_files = 0;
	char line[256];

	while ((ent = readdir(dir)) != NULL) {
		// Skip "." and ".."
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

		snprintf(line, sizeof(line), "%.*s\r\n", (int)(sizeof(line) - 3), ent->d_name);
		send(state->data_client_sock, line, strlen(line), 0);
		LOG(LOG_INFO, "[DEBUG] NLST Sent: %s\n", line);
		total_files++;
	}

	closedir(dir);
	shutdown(state->data_client_sock, SHUT_WR);
	close(state->data_client_sock);
	state->data_client_sock = -1;

	close(state->data_sock);
	state->data_sock = -1;

	LOG(LOG_INFO, "[DEBUG] NLST total files sent: %d\n", total_files);
	send_response(state->client_sock, "226 NLST Transfer complete\r\n");
}
void handle_pasv(ClientState* state, struct in_addr server_ip) {
	if (state->data_sock >= 0) close(state->data_sock);
	if (state->data_client_sock >= 0) close(state->data_client_sock);

	struct sockaddr_in data_addr = { 0 };
	data_addr.sin_family = AF_INET;
	data_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	data_addr.sin_port = 0;

	state->data_sock = socket(AF_INET, SOCK_STREAM, 0);
	bind(state->data_sock, (struct sockaddr*)&data_addr, sizeof(data_addr));
	listen(state->data_sock, 1);

	socklen_t len = sizeof(data_addr);
	getsockname(state->data_sock, (struct sockaddr*)&data_addr, &len);
	int port = ntohs(data_addr.sin_port);

	LOG(LOG_INFO, "[DEBUG] Entering PASV mode on port: %d\n", port);

	char response[256];
	snprintf(response, sizeof(response),
		"227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n",
		(server_ip.s_addr >> 0) & 0xFF,
		(server_ip.s_addr >> 8) & 0xFF,
		(server_ip.s_addr >> 16) & 0xFF,
		(server_ip.s_addr >> 24) & 0xFF,
		port >> 8,
		port & 0xFF);

	send_response(state->client_sock, response);
}
void handle_port(ClientState* state, const char* cmd) {
	int ip1, ip2, ip3, ip4, p1, p2;
	struct sockaddr_in client_addr = { 0 };

	// Parse the PORT command format: "PORT 192,168,1,2,4,1"
	if (sscanf(cmd, "PORT %d,%d,%d,%d,%d,%d",
		&ip1, &ip2, &ip3, &ip4, &p1, &p2) != 6) {
		send_response(state->client_sock, "501 Syntax error in PORT command\r\n");
		return;
	}

	// Convert parsed IP and port into proper network format
	client_addr.sin_family = AF_INET;
	client_addr.sin_addr.s_addr = htonl((ip1 << 24) | (ip2 << 16) | (ip3 << 8) | ip4);
	client_addr.sin_port = htons((p1 << 8) | p2);

	// Store client's IP and port
	memcpy(&state->client_data_addr, &client_addr, sizeof(client_addr));
	state->using_active_mode = 1;  // Switch to active mode

	LOG(LOG_INFO, "[DEBUG] PORT command received: Client IP = %d.%d.%d.%d, Port = %d\n",
		ip1, ip2, ip3, ip4, ntohs(client_addr.sin_port));

	send_response(state->client_sock, "200 PORT command successful\r\n");
}
void handle_rename(ClientState* state, const char* cmd) {
	static char oldpath[PATH_MAX] = { 0 };

	LOG(LOG_INFO, "[DEBUG] Received command: %s", cmd);

	if (strncmp(cmd, "RNFR", 4) == 0) {
		char temp[PATH_MAX] = { 0 };

		if (sscanf(cmd + 5, "\"%[^\"]\"", temp) != 1) {
			sscanf(cmd + 5, "%[^\r\n]", temp); // Read all characters until newline/carriage return
			LOG(LOG_INFO, "[DEBUG] RNFR TEST: %s\n", temp);
		}

		LOG(LOG_INFO, "[DEBUG] RNFR received: %s\n", temp);

		safe_path_join(oldpath, sizeof(oldpath), state->current_dir, temp);
		LOG(LOG_INFO, "[DEBUG] Formed source path: %s\n", oldpath);

		struct stat st;
		if (stat(oldpath, &st) != 0) {
			send_response(state->client_sock, "550 File not found\r\n");
			return;
		}

		send_response(state->client_sock, "350 Ready for destination name\r\n");
	}
	else if (strncmp(cmd, "RNTO", 4) == 0) {
		char newpath[PATH_MAX] = { 0 };

		// Extract new filename, handling quotes
		if (sscanf(cmd + 5, "\"%[^\"]\"", newpath) != 1) {
			sscanf(cmd + 5, "%[^\r\n]", newpath); // Read all characters until newline/carriage return
		}

		LOG(LOG_INFO, "[DEBUG] RNTO received: %s\n", newpath);

		char temp_dest[PATH_MAX] = { 0 };
		strncpy(temp_dest, newpath, sizeof(temp_dest));
		safe_path_join(newpath, sizeof(newpath), state->current_dir, temp_dest);

		if (rename(oldpath, newpath) == 0) {
			send_response(state->client_sock, "250 Rename successful\r\n");
		}
		else {
			send_response(state->client_sock, "550 Rename failed\r\n");
		}
	}
}
void handle_rest(ClientState* state, const char* cmd) {
	long offset = 0;
	LOG(LOG_INFO, "[DEBUG] Entering HANDLE REST: %s\n", cmd);

	// Parse "REST <offset>"
	if (sscanf(cmd, "REST %ld", &offset) == 1 && offset >= 0) {
		state->resume_offset = offset;
		send_response(state->client_sock, "350 Restart position accepted (%ld)\r\n", offset);
	}
	else {
		send_response(state->client_sock, "501 Invalid REST argument\r\n");
	}
}
void handle_retr(ClientState* state, const char* filename) {
	LOG(LOG_INFO, "[DEBUG] handle_retr called for file: %s\n", filename);

	char path[PATH_MAX];
	safe_path_join(path, sizeof(path), state->current_dir, filename);

	// Check if file exists and is a regular file
	int type = get_file_type(path);
	if (type != 0) { // Not a regular file
		LOG(LOG_ERROR, "[ERROR] Invalid file type or file not found: %s\n", path);
		send_response(state->client_sock, "550 File not found or invalid type\r\n");
		return;
	}

	// Get file size for progress tracking
	struct stat st;
	long total_file_size = 0;
	if (stat(path, &st) == 0) {
		total_file_size = st.st_size;
		LOG(LOG_INFO, "[RETR] File size: %ld bytes\n", total_file_size);
	}

	// Establish data connection
	if (state->using_active_mode) {
		state->data_client_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (connect(state->data_client_sock, (struct sockaddr*)&state->client_data_addr, sizeof(state->client_data_addr)) < 0) {
			LOG(LOG_ERROR, "[ERROR] Failed to establish active mode connection\n");
			send_response(state->client_sock, "425 Can't open data connection\r\n");
			return;
		}
	}
	else {
		struct sockaddr_in data_client_addr;
		socklen_t data_client_len = sizeof(data_client_addr);
		state->data_client_sock = accept(state->data_sock, (struct sockaddr*)&data_client_addr, &data_client_len);
		if (state->data_client_sock < 0) {
			LOG(LOG_ERROR, "[ERROR] Failed to accept passive mode connection\n");
			send_response(state->client_sock, "425 Can't open data connection\r\n");
			return;
		}
	}

	FILE* file = fopen(path, "rb");
	if (!file) {
		LOG(LOG_ERROR, "[ERROR] Failed to open file: %s\n", path);
		send_response(state->client_sock, "550 Failed to open file\r\n");
		close(state->data_client_sock);
		return;
	}

	// Seek to resume offset if set
	long start_position = state->resume_offset;
	if (start_position > 0) {
		LOG(LOG_INFO, "[DEBUG] Seeking to offset: %ld\n", start_position);
		if (fseek(file, start_position, SEEK_SET) != 0) {
			LOG(LOG_ERROR, "[ERROR] fseek failed! Unable to resume transfer.\n");
			fclose(file);
			send_response(state->client_sock, "550 Failed to seek file\r\n");
			close(state->data_client_sock);
			return;
		}
		// Adjust total size for resume
		if (total_file_size > 0) {
			total_file_size -= start_position;
		}
	}

	send_response(state->client_sock, "150 Opening data connection\r\n");

	// Clear console and prepare for progress updates
	infoCons.clear();

	// Send file data with smart progress updates
	char buffer[BUFFER_SIZE];
	ssize_t bytes_read, bytes_sent;
	long total_bytes_sent = 0;

	// For progress tracking
	auto transfer_start = std::chrono::steady_clock::now();
	auto last_update_time = transfer_start;
	long last_update_bytes = 0;

	// For transfer rate calculation
	double current_rate_kbps = 0.0;

	while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		bytes_sent = send(state->data_client_sock, buffer, bytes_read, 0);

		if (bytes_sent < 0) {
			LOG(LOG_ERROR, "[ERROR] send() failed, errno: %d\n", errno);
			fclose(file);
			close(state->data_client_sock);
			send_response(state->client_sock, "426 Connection closed; transfer aborted.\r\n");

			infoCons.clear();
			infoCons.out("Download failed: &connection closed&");
			infoCons.nl();
			return;
		}

		total_bytes_sent += bytes_sent;

		auto current_time = std::chrono::steady_clock::now();
		auto time_since_last_update = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_update_time).count();

		// Update display every 100ms minimum, or when significant data sent
		if (time_since_last_update >= 100 || (total_bytes_sent - last_update_bytes) > 50000) {
			// Calculate transfer rate
			if (time_since_last_update > 0) {
				long bytes_since_last = total_bytes_sent - last_update_bytes;
				double seconds = time_since_last_update / 1000.0;
				current_rate_kbps = (bytes_since_last / 1024.0) / seconds;
			}

			// Build status message based on available information
			infoCons.clear();
			if (total_file_size > 0) {
				// We know total size - show percentage
				long actual_total_sent = total_bytes_sent + start_position;
				long actual_total_size = total_file_size + start_position;
				int percent = (int)((actual_total_sent * 100) / actual_total_size);

				infoCons.out(format("Downloading &%s&: &%d%%& (%s/%s) &%.1f& KB/s", filename, percent, format_size(actual_total_sent).c_str(), format_size(actual_total_size).c_str(), current_rate_kbps));
			}
			else {
				// Don't know total size - show progress and rate
				infoCons.out(format("Downloading &%s&: &%s& - &%.1f& KB/s", filename, format_size(total_bytes_sent + start_position).c_str(), current_rate_kbps));
			}

			infoCons.nl();

			last_update_time = current_time;
			last_update_bytes = total_bytes_sent;
		}
	}

	// Calculate final statistics
	auto transfer_end = std::chrono::steady_clock::now();
	auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(transfer_end - transfer_start).count();
	double total_rate_kbps = (total_duration > 0) ? (total_bytes_sent / 1024.0) / (total_duration / 1000.0) : 0;

	fclose(file);
	close(state->data_client_sock);
	state->data_client_sock = -1;
	state->resume_offset = 0; // Reset resume offset after transfer

	send_response(state->client_sock, "226 Transfer complete\r\n");

	// Show final status with summary
	infoCons.clear();
	long actual_total_sent = total_bytes_sent + start_position;

	if (total_file_size > 0) {
		infoCons.out(format("&%s& downloaded: &%s& (100%%) in &%.1fs& (%.1f KB/s)", filename, format_size(actual_total_sent).c_str(), total_duration / 1000.0, total_rate_kbps));
	}
	else {
		infoCons.out(format("&%s& downloaded: &%s& in &%.1fs& (%.1f KB/s)", filename, format_size(actual_total_sent).c_str(), total_duration / 1000.0, total_rate_kbps));
	}
	infoCons.nl();
	infoCons.nl();

	LOG(LOG_INFO, "[DEBUG] Download complete. Total bytes sent: %ld, Duration: %.1fs, Avg Rate: %.1f KB/s\n",
		total_bytes_sent, total_duration / 1000.0, total_rate_kbps);
}
void handle_rmd(ClientState* state, const char* dir_name) {
	if (dir_name == NULL || strlen(dir_name) == 0) {
		send_response(state->client_sock, "550 Invalid directory name\r\n");
		return;
	}

	LOG(LOG_INFO, "[DEBUG] Request to remove directory: %s", dir_name);

	char full_path[PATH_MAX];

	// Construct full path safely
	if (dir_name[0] == '/') {
		snprintf(full_path, sizeof(full_path), "sdmc:%s", dir_name);
	}
	else {
		safe_path_join(full_path, sizeof(full_path), state->current_dir, dir_name);
	}
	full_path[PATH_MAX - 1] = '\0';

	// Ensure no accidental trailing spaces, \r, or \n
	size_t len = strlen(full_path);
	while (len > 0 && (full_path[len - 1] == '\r' || full_path[len - 1] == '\n' || full_path[len - 1] == ' ')) {
		full_path[len - 1] = '\0';
		len--;
	}

	LOG(LOG_INFO, "[DEBUG] Final path for deletion: %s\n", full_path);

	if (recursive_delete(full_path) == 0) {
		send_response(state->client_sock, "250 Directory deleted successfully\r\n");
		LOG(LOG_INFO, "[DEBUG] Directory deleted: %s\n", full_path);
	}
	else {
		send_response(state->client_sock, "550 Failed to delete directory\r\n");
		LOG(LOG_ERROR, "[ERROR] Failed to delete directory: %s\n", full_path);
	}
}
void handle_stor(ClientState* state, const char* filename) {
	LOG(LOG_INFO, "[DEBUG] handle_stor called for file: %s\n", filename);

	char path[PATH_MAX];
	safe_path_join(path, sizeof(path), state->current_dir, filename);

	// Reset expected size at start of transfer (will be set if known)
	state->expected_file_size = 0;

	// Establish data connection (your existing code)
	if (state->using_active_mode) {
		state->data_client_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (connect(state->data_client_sock, (struct sockaddr*)&state->client_data_addr, sizeof(state->client_data_addr)) < 0) {
			LOG(LOG_ERROR, "[ERROR] Failed to establish active mode connection\n");
			send_response(state->client_sock, "425 Can't open data connection\r\n");
			return;
		}
	}
	else {
		struct sockaddr_in data_client_addr;
		socklen_t data_client_len = sizeof(data_client_addr);
		state->data_client_sock = accept(state->data_sock, (struct sockaddr*)&data_client_addr, &data_client_len);
		if (state->data_client_sock < 0) {
			LOG(LOG_ERROR, "[ERROR] Failed to accept passive mode connection\n");
			send_response(state->client_sock, "425 Can't open data connection\r\n");
			return;
		}
	}

	// Determine file open mode
	const char* mode = "wb";
	struct stat st;
	if (state->resume_offset > 0 && stat(path, &st) == 0) {
		mode = "r+b";
		// If resuming, we know the total size from the existing file
		state->expected_file_size = st.st_size;
		LOG(LOG_INFO, "[RESUME] Resuming upload, total size: %ld bytes\n", state->expected_file_size);
	}

	FILE* file = fopen(path, mode);
	if (!file) {
		LOG(LOG_ERROR, "[ERROR] Failed to open file: %s\n", path);
		send_response(state->client_sock, "550 Failed to open file\r\n");
		close(state->data_client_sock);
		return;
	}

	// Seek to resume offset if needed
	if (state->resume_offset > 0) {
		LOG(LOG_INFO, "[DEBUG] Seeking to offset: %ld\n", state->resume_offset);
		if (fseek(file, state->resume_offset, SEEK_SET) != 0) {
			LOG(LOG_ERROR, "[ERROR] fseek failed! Unable to resume upload.\n");
			fclose(file);
			send_response(state->client_sock, "550 Failed to seek file\r\n");
			close(state->data_client_sock);
			return;
		}
	}

	send_response(state->client_sock, "150 Opening data connection\r\n");

	// Clear console and prepare for progress updates
	infoCons.clear();

	// Receive and write data with smart progress updates
	char buffer[BUFFER_SIZE];
	ssize_t bytes_received, total_bytes_written = state->resume_offset;

	// For progress tracking
	auto transfer_start = std::chrono::steady_clock::now();
	auto last_update_time = transfer_start;
	long last_update_bytes = total_bytes_written;

	// For transfer rate calculation
	double current_rate_kbps = 0.0;
	int update_count = 0;

	while ((bytes_received = recv(state->data_client_sock, buffer, sizeof(buffer), 0)) > 0) {
		fwrite(buffer, 1, bytes_received, file);
		total_bytes_written += bytes_received;

		auto current_time = std::chrono::steady_clock::now();
		auto time_since_last_update = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_update_time).count();

		// Update display every 100ms minimum, or when significant data received
		if (time_since_last_update >= 100 || (total_bytes_written - last_update_bytes) > 50000) {
			// Calculate transfer rate
			if (time_since_last_update > 0) {
				long bytes_since_last = total_bytes_written - last_update_bytes;
				double seconds = time_since_last_update / 1000.0;
				current_rate_kbps = (bytes_since_last / 1024.0) / seconds;
			}

			// Build status message based on available information
			infoCons.clear();
			if (state->expected_file_size > 0) {
				// We know total size - show percentage
				int percent = (int)((total_bytes_written * 100) / state->expected_file_size);
				infoCons.out(format("Uploading &%s&: &%d%%& (%s/%s) &%.1f& KB/s", filename, percent, format_size(total_bytes_written).c_str(), format_size(state->expected_file_size).c_str(), current_rate_kbps));
			}
			else {
				// Don't know total size - show progress and rate
				infoCons.out(format("Uploading &%s&: &%s& - &%.1f& KB/s", filename, format_size(total_bytes_written).c_str(), current_rate_kbps));
			}

			infoCons.nl();
			infoCons.nl();

			last_update_time = current_time;
			last_update_bytes = total_bytes_written;
			update_count++;
		}
	}

	// Calculate final statistics
	auto transfer_end = std::chrono::steady_clock::now();
	auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(transfer_end - transfer_start).count();
	double total_rate_kbps = (total_duration > 0) ? (total_bytes_written / 1024.0) / (total_duration / 1000.0) : 0;

	if (bytes_received < 0) {
		LOG(LOG_ERROR, "[ERROR] recv() failed, errno: %d\n", errno);
		send_response(state->client_sock, "426 Connection closed, transfer aborted.\r\n");
		infoCons.clear();
		infoCons.out(format("Connection closed: &transfer aborted&"));
		infoCons.nl();
	}
	else {
		send_response(state->client_sock, "226 Transfer complete\r\n");

		// Show final status with summary
		infoCons.clear();
		//char final_msg[256];
		if (state->expected_file_size > 0) {
			infoCons.out(format("&%s& upload complete: &%s& (100%%) in &%.1fs& (%.1f KB/s)", filename, format_size(total_bytes_written).c_str(), total_duration / 1000.0, total_rate_kbps));
		}
		else {
			infoCons.out(format("&%s& upload completed: &%s& in &%.1fs& (%.1f KB/s)", filename, format_size(total_bytes_written).c_str(), total_duration / 1000.0, total_rate_kbps));
		}
		infoCons.nl();
	}

	fclose(file);
	close(state->data_client_sock);
	state->data_client_sock = -1;
	state->resume_offset = 0;
	state->expected_file_size = 0;  // Reset for next transfer

	LOG(LOG_INFO, "[DEBUG] Upload complete. Total bytes received: %ld, Duration: %.1fs, Avg Rate: %.1f KB/s\n",
		total_bytes_written, total_duration / 1000.0, total_rate_kbps);
}
void handle_size(ClientState* state, const char* filename) {
	char path[PATH_MAX];
	safe_path_join(path, sizeof(path), state->current_dir, filename);

	struct stat st;
	if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
		state->expected_file_size = st.st_size;  // Store for progress calculation
		send_response(state->client_sock, "213 %ld\r\n", st.st_size);
		LOG(LOG_INFO, "[SIZE] File: %s, Size: %ld bytes\n", filename, st.st_size);
	}
	else {
		state->expected_file_size = 0;  // Reset if file not found
		send_response(state->client_sock, "550 File not found\r\n");
		LOG(LOG_INFO, "[SIZE] File not found: %s\n", filename);
	}

}
void ftpClientThread(void* arg) {
	ftpThreadArgs* a = (ftpThreadArgs*)arg;
	console* c = a->c;
	ClientState* state = a->clientState;
	bool* threadFinishedFlag = a->thFin;  // This points to g_thread_active[i]

	int authenticated = 0;
	char user[256] = { 0 };
	char pass[256] = { 0 };

	send_response(state->client_sock, "220 Nintendo Switch FTP Server\r\n");

	char buffer[1024];

	while (g_server_running) {
		memset(buffer, 0, sizeof(buffer));

		// Use select() to make recv() non-blocking with timeout
		fd_set readfds;
		struct timeval timeout;

		FD_ZERO(&readfds);
		FD_SET(state->client_sock, &readfds);
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		if (start_logs) {
			g_ftp_config.logging_enabled = true;
		}
		else {
			g_ftp_config.logging_enabled = false;
		}

		int select_result = select(state->client_sock + 1, &readfds, NULL, NULL, &timeout);

		if (!g_server_running) {
			break;  // Exit if server is shutting down
		}

		if (select_result > 0 && FD_ISSET(state->client_sock, &readfds)) {
			int received = recv(state->client_sock, buffer, sizeof(buffer) - 1, 0);
			if (received <= 0) {
				break;  // Client disconnected
			}

			buffer[received] = '\0';

			// https://en.wikipedia.org/wiki/List_of_FTP_commands
			// Change up to parent directory
			if (strncmp(buffer, "CDUP", 4) == 0) {
				handle_cdup(state);
			}
			// Change directory
			else if (strncmp(buffer, "CWD", 3) == 0) {
				char new_dir[PATH_MAX] = { 0 };
				if (sscanf(buffer + 4, "\"%[^\"]\"", new_dir) != 1) {
					sscanf(buffer + 4, "%[^\r\n]", new_dir);
				}
				handle_cwd(state, new_dir);
			}
			// Delete the specified file
			else if (strncmp(buffer, "DELE", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				char filename[PATH_MAX] = { 0 };
				if (sscanf(buffer + 5, "\"%[^\"]\"", filename) != 1) {
					sscanf(buffer + 5, "%[^\r\n]", filename);
				}
				handle_dele(state, filename);
			}
			// Show client the available features
			else if (strncmp(buffer, "FEAT", 4) == 0) {
				send_response(state->client_sock, "211 Features:\r\n REST STREAM\r\n MOTD\r\n211 End\r\n");
			}
			// Show List of files in current working directory
			else if (strncmp(buffer, "LIST", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				send_response(state->client_sock, "150 Opening data connection\r\n");
				handle_list(state);
			}
			// Make directory
			else if (strncmp(buffer, "MKD ", 4) == 0) {
				handle_mkd(state, buffer + 4);
			}
			// Similar to list command - meant to standardize the format for directory listings for ftp clients
			else if (strncmp(buffer, "MLSD", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				send_response(state->client_sock, "150 Opening data connection\r\n");
				handle_mlsd(state);
			}
			// Moving folder and files
			else if (strncmp(buffer, "MV ", 3) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				handle_mv(state, buffer);
			}
			// Unlike the LIST command, only the list of files and no other information on those files
			else if (strncmp(buffer, "NLST", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				send_response(state->client_sock, "150 Opening data connection\r\n");
				handle_nlst(state);
			}
			// Handle user password
			else if (strncmp(buffer, "PASS", 4) == 0) {
				sscanf(buffer, "PASS %255s", pass);
				if (strcmp(user, get_ftp_username()) == 0 && strcmp(pass, get_ftp_password()) == 0) {
					authenticated = 1;
					client_connected = true;
					// Use console for output
					c->clear();
					c->out("Nintendo Switch FTP Server Client: #Login successful#");
					c->nl();
					send_response(state->client_sock, "230 Login successful\r\n");
					// Send MOTD
					char MOTD[512];
					handle_motd(MOTD, sizeof(MOTD));
					send_response(state->client_sock, MOTD);
				}
				else {
					c->clear();
					send_response(state->client_sock, "530 Login incorrect\r\n");
					infoCons.out(format("Login incorrect, Try:\n"));
					c->nl();
					infoCons.out(format("Username: &%s&", get_ftp_username()));
					c->nl();
					infoCons.out(format("Password: &%s&", get_ftp_password()));
					c->nl();
					infoCons.out(format("Port: &%d&", get_ftp_port()));
					c->nl();
				}
			}
			// Enter a passive FTP session
			else if (strncmp(buffer, "PASV", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				struct sockaddr_in server_addr;
				socklen_t server_len = sizeof(server_addr);
				getsockname(state->client_sock, (struct sockaddr*)&server_addr, &server_len);
				handle_pasv(state, server_addr.sin_addr);
			}
			// Handle connection port
			else if (strncmp(buffer, "PORT", 4) == 0) {
				handle_port(state, buffer);
			}
			// Displays the current working directory
			else if (strncmp(buffer, "PWD", 3) == 0) {
				send_response(state->client_sock, "257 \"%s\"\r\n", state->current_dir);
			}
			// Quit server
			else if (strncmp(buffer, "QUIT", 4) == 0) {
				send_response(state->client_sock, "221 Goodbye\r\n");
				break;
			}
			// Handles downloaded file resuming
			else if (strncmp(buffer, "REST ", 5) == 0) {
				handle_rest(state, buffer);
			}
			// RMD (Remove directory)
			else if (strncmp(buffer, "RMD ", 4) == 0) {
				handle_rmd(state, buffer + 4);
			}
			// RNFR (Rename From)
			else if (strncmp(buffer, "RNFR", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				handle_rename(state, buffer);
			}
			// Download files from switch
			else if (strncmp(buffer, "RETR", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				char filename[PATH_MAX] = { 0 };
				if (sscanf(buffer + 5, "\"%[^\"]\"", filename) != 1) {
					sscanf(buffer + 5, "%[^\r\n]", filename);
				}
				handle_retr(state, filename);
			}
			// Upload files to switch
			else if (strncmp(buffer, "STOR", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				char filename[PATH_MAX] = { 0 };
				if (sscanf(buffer + 5, "\"%[^\"]\"", filename) != 1) {
					sscanf(buffer + 5, "%[^\r\n]", filename);
				}
				handle_stor(state, filename);
			}
			// RNTO (Rename To)
			else if (strncmp(buffer, "RNTO", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				handle_rename(state, buffer);
			}
			// Used for setting file and folder permissions
			else if (strncmp(buffer, "SITE CHMOD", 10) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				handle_chmod(state, buffer);
			}
			// Asks for information about the server's operating system
			else if (strncmp(buffer, "SYST", 4) == 0) {
				send_response(state->client_sock, "215 UNIX Type: L8\r\n");
			}
			// Inform the server of the type of data that is being transferred
			else if (strncmp(buffer, "TYPE", 4) == 0) {
				send_response(state->client_sock, "200 Type set to ASCII\r\n");
			}
			// Handles username
			else if (strncmp(buffer, "USER", 4) == 0) {
				sscanf(buffer, "USER %255s", user);
				send_response(state->client_sock, "331 Password required\r\n");
			}
			// Return the size of a file
			else if (strncmp(buffer, "SIZE", 4) == 0) {
				if (!authenticated) {
					send_response(state->client_sock, "530 Not logged in\r\n");
					continue;
				}
				char filename[PATH_MAX] = { 0 };
				if (sscanf(buffer + 5, "\"%[^\"]\"", filename) != 1) {
					sscanf(buffer + 5, "%[^\r\n]", filename);
				}
				handle_size(state, filename);
			}
			// Unknown command
			else {
				send_response(state->client_sock, "500 Unknown command\r\n");
			}

		}
		else if (select_result < 0) {
			break;  // Error occurred
		}
	}

	// === CLEANUP - THIS IS CRITICAL ===
	client_connected = false;
	connected_clients--;

	// Close all sockets
	if (state->client_sock >= 0) {
		shutdown(state->client_sock, SHUT_RDWR);
		close(state->client_sock);
		state->client_sock = -1;
	}
	if (state->data_sock >= 0) {
		close(state->data_sock);
		state->data_sock = -1;
	}
	if (state->data_client_sock >= 0) {
		close(state->data_client_sock);
		state->data_client_sock = -1;
	}

	remove_client(state);
	free(state);

	c->clear();
	c->out("Nintendo Switch FTP Server Client: #Disconnected#");
	c->nl();

	// === CRITICAL: Signal thread completion ===
	*threadFinishedFlag = false;  // Set to FALSE to indicate thread is finished
	ftpThreadArgsDestroy(a);

	// Log the thread completion
	LOG(LOG_INFO, "FTP client thread finished and cleaned up\n");
}
// FTP server initialization
bool ftp_init(void) {
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&g_ftp_pad);

	if (R_FAILED(socketInitializeDefault())) {
		return false;
	}

	// Load configuration
	if (!load_ftp_config()) {
		// Create default config file if none exists
		create_default_config();
	}

	// Initialize client arrays based on config
	int max_clients = get_max_clients();

	g_active_clients = (ClientState**)calloc(max_clients, sizeof(ClientState*));
	g_thread_active = (bool*)calloc(max_clients, sizeof(bool));
	g_client_threads = (Thread*)calloc(max_clients, sizeof(Thread));

	if (!g_active_clients || !g_thread_active || !g_client_threads) {
		LOG(LOG_ERROR, "Failed to allocate client arrays\n");
		return false;
	}

	mutexInit(&g_clients_mutex);

	LOG(LOG_INFO, "FTP initialized - Port: %d, Max Clients: %d, Logging: %s\n",
		get_ftp_port(), get_max_clients(), get_logging_enabled() ? "enabled" : "disabled");

	return true;
}
// Start FTP server
void ftp_start(PadState* pad) {
	if (g_ftp_running) return;

	g_server_running = true;
	g_ftp_running = true;

	// Initialize control socket
	control_sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server_addr = { 0 };
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(get_ftp_port());
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	bind(control_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));

	// Make socket non-blocking
	int flags = fcntl(control_sock, F_GETFL, 0);
	fcntl(control_sock, F_SETFL, flags | O_NONBLOCK);

	listen(control_sock, 5);

	LOG(LOG_INFO, "FTP server started on port %d\n", get_ftp_port());
}

void ftp_stop(PadState* pad) {
	if (!g_ftp_running) return;

	g_server_running = false;
	g_ftp_running = false;

	int max_clients = get_max_clients();

	// Wait for all client threads to finish
	for (int i = 0; i < max_clients; i++) {
		if (g_thread_active[i]) {
			threadWaitForExit(&g_client_threads[i]);
			threadClose(&g_client_threads[i]);
			// Cleanup thread arguments if they exist
			if (g_active_clients[i] != NULL) {
				// Note: The thread arguments are cleaned up in the thread itself
				// when ftpThreadArgsDestroy is called
			}
			g_thread_active[i] = false;
		}
	}

	// Cleanup clients
	mutexLock(&g_clients_mutex);
	for (int i = 0; i < max_clients; i++) {
		if (g_active_clients[i] != NULL) {
			ClientState* client = g_active_clients[i];
			if (client->client_sock >= 0) {
				shutdown(client->client_sock, SHUT_RDWR);
				close(client->client_sock);
			}
			if (client->data_sock >= 0) close(client->data_sock);
			if (client->data_client_sock >= 0) close(client->data_client_sock);
			free(client);
			g_active_clients[i] = NULL;
		}
	}
	mutexUnlock(&g_clients_mutex);

	if (control_sock >= 0) {
		close(control_sock);
		control_sock = -1;
	}

	free(g_active_clients);
	free(g_thread_active);
	free(g_client_threads);
	g_active_clients = NULL;
	g_thread_active = NULL;
	g_client_threads = NULL;

	LOG(LOG_INFO, "FTP server stopped\n");
	socketExit();
}

// Cleanup FTP resources
void ftp_cleanup(PadState* pad) {  // Add PadState* parameter
	ftp_stop(pad);                 // Pass the pad parameter
	socketExit();
}

// Check if FTP server is running
bool ftp_is_running(void) {
	return g_ftp_running;
}

// FTP server main loop - call this periodically from your main program
void ftp_update(void) {
	if (!g_ftp_running) return;

	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	ClientState* state = (ClientState*)calloc(1, sizeof(ClientState));

	state->client_sock = accept(control_sock, (struct sockaddr*)&client_addr, &client_len);

	if (state->client_sock >= 0) {
		strncpy(state->current_dir, "/", PATH_MAX);
		normalize_path(state->current_dir);
		state->data_sock = -1;
		state->data_client_sock = -1;

		int max_clients = get_max_clients();

		// Find first available slot
		int available_slot = -1;
		for (int i = 0; i < max_clients; i++) {
			if (!g_thread_active[i]) {
				available_slot = i;
				break;
			}
		}

		if (available_slot != -1) {
			// We have an available slot
			add_client(state);

			// Create thread arguments with console
			ftpThreadArgs* threadArgs = ftpThreadArgsCreate(&infoCons, &g_thread_active[available_slot], state);

			// Mark slot as active BEFORE starting thread
			g_thread_active[available_slot] = true;

			Result rc = threadCreate(&g_client_threads[available_slot], ftpClientThread, threadArgs, NULL, 0x20000, 0x2B, -2);
			if (R_SUCCEEDED(rc)) {
				rc = threadStart(&g_client_threads[available_slot]);
				if (R_SUCCEEDED(rc)) {
					connected_clients++;
					infoCons.out(format("New FTP client connected on: #thread %d#", available_slot));
					infoCons.nl();
					LOG(LOG_INFO, "Client connected on thread %d\n", available_slot);
				}
				else {
					// Thread start failed - cleanup
					LOG(LOG_ERROR, "Thread start failed: 0x%x\n", rc);
					g_thread_active[available_slot] = false;
					threadClose(&g_client_threads[available_slot]);
					remove_client(state);
					send_response(state->client_sock, "421 Server error - please try again\r\n");
					close(state->client_sock);
					free(state);
					ftpThreadArgsDestroy(threadArgs);
				}
			}
			else {
				// Thread creation failed - cleanup
				LOG(LOG_ERROR, "Thread creation failed: 0x%x\n", rc);
				g_thread_active[available_slot] = false;
				remove_client(state);
				send_response(state->client_sock, "421 Server error - please try again\r\n");
				close(state->client_sock);
				free(state);
				ftpThreadArgsDestroy(threadArgs);
			}
		}
		else {
			// Maximum clients reached - reject new connection
			send_response(state->client_sock, "421 Maximum clients reached - try again later\r\n");
			close(state->client_sock);
			free(state);

			infoCons.out(format("Maximum FTP clients reached (%d) - connection rejected", max_clients));
			infoCons.nl();
		}
	}
	else {
		// No client waiting to connect, free the state
		free(state);
	}

	// Small delay to prevent excessive CPU usage
	svcSleepThread(1000000);
}

bool user_connected(void) {
	return client_connected;
}

ftpThreadArgs* ftpThreadArgsCreate(console* c, bool* status, ClientState* client) {
	ftpThreadArgs* ret = new ftpThreadArgs;
	ret->c = c;
	ret->thFin = status;
	ret->clientState = client;
	return ret;
}

void ftpThreadArgsDestroy(ftpThreadArgs* a) {
	delete a;
}