/*
CHECKPOINT_HANDLER_KEYS.H

The checkpoint handler's keys: change them here. A key is one of
HALO_MOD_KEY_* (port/linux/include/halo_mod.h); with its _CTRL TRUE it works
only together with Ctrl.
*/

#ifndef __CHECKPOINT_HANDLER_KEYS_H
#define __CHECKPOINT_HANDLER_KEYS_H

/* take a checkpoint */
#define CHECKPOINT_SET_KEY HALO_MOD_KEY_F5
#define CHECKPOINT_SET_CTRL FALSE

/* go back to the latest checkpoint */
#define CHECKPOINT_RESTORE_KEY HALO_MOD_KEY_F9
#define CHECKPOINT_RESTORE_CTRL FALSE

#endif
