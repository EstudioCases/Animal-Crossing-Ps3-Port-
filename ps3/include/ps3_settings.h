#ifndef PS3_SETTINGS_H
#define PS3_SETTINGS_H

typedef struct PS3Settings {
    int disable_resetti;
} PS3Settings;

extern PS3Settings g_ps3_settings;

void ps3_settings_load(void);

#endif /* PS3_SETTINGS_H */

