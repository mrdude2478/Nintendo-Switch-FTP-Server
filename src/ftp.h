/*
Nintendo Switch FTP Plugin
Created By MrDude
*/

#ifndef FTP_H
#define FTP_H

#include <switch.h>
#include "../inc/cons.h"

// ClientState structured variables - MOVE THIS BEFORE ftpThreadArgs
typedef struct {
	char current_dir[PATH_MAX];
	char rename_path[PATH_MAX];
	int client_sock;
	int data_sock;
	int data_client_sock;
	struct sockaddr_in data_addr;
	struct sockaddr_in client_data_addr;
	long resume_offset;
	int using_active_mode;
	long expected_file_size;
} ClientState;

// Add to dumpArgs equivalent for FTP
typedef struct {
	console* c;
	bool* thFin;
	ClientState* clientState;
} ftpThreadArgs;

#ifdef __cplusplus
extern "C" {
#endif

	// FTP server control functions
	bool ftp_init(void);
	void ftp_start(PadState* pad);
	void ftp_stop(PadState* pad);
	void ftp_cleanup(PadState* pad);
	bool ftp_is_running(void);
	void ftp_update(void);
	bool user_connected(void);
	extern bool start_logs;  //also set this in main
	extern int g_max_clients_override;
	extern int connected_clients;

	// Update function declarations
	ftpThreadArgs* ftpThreadArgsCreate(console* c, bool* status, ClientState* client);
	void ftpThreadArgsDestroy(ftpThreadArgs* a);
	void ftpClientThread(void* arg);

#ifdef __cplusplus
}
#endif

#endif // FTP_H