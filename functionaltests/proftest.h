#ifndef __H_PROFTEST
#define __H_PROFTEST

#define XDG_CONFIG_HOME "./functionaltests/files/xdg_config_home"
#define XDG_DATA_HOME   "./functionaltests/files/xdg_data_home"

void init_prof_test(void **state);
void close_prof_test(void **state);

void prof_start(void);
void prof_connect(char *jid, char *password);
void prof_input(char *input);

int prof_output_exact(char *text);
int prof_output_regex(char *text);

#endif