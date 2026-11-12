// TypeA360Clap.h
// The three entry functions clap-wrapper links into every format.

#pragma once

bool tbtypea_entry_init (const char* pluginPath);
void tbtypea_entry_deinit();
const void* tbtypea_entry_get_factory (const char* factoryId);
