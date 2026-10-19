#ifndef PSX3D_DEVNET_H
#define PSX3D_DEVNET_H
#include "devtools.h"
#define DEV_COMMAND_MAX 16384
#define DEV_HISTORY 60
#define DEV_METRICS 88
/* Numeric JSON/TSV columns, kept in one order for live and batch output. */
extern const char *const dev_columns[DEV_METRICS];
typedef struct {
	u64 seq, ms;
	double v[DEV_METRICS];
	char audio[256], pica[1024], probe[512];
} dev_measurement;
void devnet_init(void);
void devnet_notification_done(void);
bool devnet_stopping(void);
void devnet_exit(void);
bool devnet_take(char *command);
void devnet_reply(const char *json);
void devnet_status(const char *json);
void devnet_sample(const dev_measurement *s, bool reset);
bool devnet_shot(const char *path, bool bottom); /* main copies only; worker encodes */
void devnet_start_update(const char *args);
void devnet_update_reply(const char *args); /* after current game fully unloaded */
#endif
