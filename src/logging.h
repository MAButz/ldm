#define LOGFILE "/var/log/ldm.log"
#define LOGLEVEL 6
void log_entry(char *component, int level, const char *format, ...);
void die(char *component, const char *format, ...);
void log_close();
void log_init();
