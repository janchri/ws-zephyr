/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * "ww" shell commands: playback control and playlist management, plus the
 * simulated tag reader, button and volume pot used on native_sim.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/shell/shell.h>

#ifdef CONFIG_WW_KEYS_SIM
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#endif

#include "wirbelwind.h"

/* Playback */

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct ww_status st;

	ww_controller_get_status(&st);

	shell_print(sh, "tag:      %s", st.tag_present ? st.uid : "-");
	shell_print(sh, "playlist: %s", st.uid[0] ? st.name : "-");
	if (st.num_tracks) {
		shell_print(sh, "track:    %u/%u %s", st.track + 1, st.num_tracks, st.track_path);
	}
	shell_print(sh, "state:    %s", ww_player_state_str(st.player.state));
	if (st.player.state != WW_PLAYER_STOPPED) {
		shell_print(sh, "position: %u:%02u (%u Hz, %u ch, %u kbit/s)",
			    st.player.pos_ms / 60000, (st.player.pos_ms / 1000) % 60,
			    st.player.rate, st.player.channels, st.player.bitrate_kbps);
	}
	shell_print(sh, "volume:   %u/%u", st.volume, CONFIG_WW_VOLUME_MAX);

	return 0;
}

#define SIMPLE_CMD(_name, _cmd)                                                                    \
	static int cmd_##_name(const struct shell *sh, size_t argc, char **argv)                   \
	{                                                                                          \
		return ww_event_post_cmd(_cmd, 0);                                                 \
	}

SIMPLE_CMD(next, WW_CMD_NEXT)
SIMPLE_CMD(reset, WW_CMD_RESET)
SIMPLE_CMD(reset_all, WW_CMD_RESET_ALL)

static int cmd_vol(const struct shell *sh, size_t argc, char **argv)
{
	struct ww_status st;

	if (argc < 2) {
		ww_controller_get_status(&st);
		shell_print(sh, "%u/%u", st.volume, CONFIG_WW_VOLUME_MAX);
		return 0;
	}

	return ww_event_post_cmd(WW_CMD_VOLUME, strtol(argv[1], NULL, 0));
}

static int cmd_seek(const struct shell *sh, size_t argc, char **argv)
{
	return ww_event_post_cmd(WW_CMD_SEEK, strtol(argv[1], NULL, 0));
}

static int cmd_track(const struct shell *sh, size_t argc, char **argv)
{
	return ww_event_post_cmd(WW_CMD_TRACK, strtol(argv[1], NULL, 0) - 1);
}

/* Playlists */

static int print_playlist(const char *uuid, void *user_data)
{
	const struct shell *sh = user_data;
	struct ww_playlist *pl = ww_playlist_scratch_lock();

	if (ww_playlist_load(pl, uuid) == 0) {
		shell_print(sh, "%-20s %3u tracks  %s", uuid, pl->d.num_tracks, pl->d.name);
	} else {
		shell_print(sh, "%-20s (corrupt)", uuid);
	}
	ww_playlist_scratch_unlock(false);

	return 0;
}

static int cmd_pl_list(const struct shell *sh, size_t argc, char **argv)
{
	return ww_playlist_foreach(print_playlist, (void *)sh);
}

static void join_args(char *buf, size_t len, size_t argc, char **argv)
{
	buf[0] = '\0';
	for (size_t i = 0; i < argc; i++) {
		if (i) {
			strncat(buf, " ", len - strlen(buf) - 1);
		}
		strncat(buf, argv[i], len - strlen(buf) - 1);
	}
}

enum pl_op { PL_SHOW, PL_CREATE, PL_NAME, PL_ADD, PL_RM, PL_DEL };

static int pl_edit(const struct shell *sh, enum pl_op op, size_t argc, char **argv)
{
	struct ww_playlist *pl = ww_playlist_scratch_lock();
	const char *uuid = argv[1];
	char name[64];
	int ret = 0;

	if (op != PL_CREATE && op != PL_DEL) {
		ret = ww_playlist_load(pl, uuid);
		if (ret) {
			shell_error(sh, "Cannot load playlist %s: %d", uuid, ret);
			goto out;
		}
	}

	switch (op) {
	case PL_SHOW:
		shell_print(sh, "%s: %s", pl->d.uuid, pl->d.name);
		for (size_t i = 0; i < pl->d.num_tracks; i++) {
			shell_print(sh, "%3u %s", i + 1, pl->d.tracks[i]);
		}
		break;
	case PL_CREATE:
		join_args(name, sizeof(name), argc - 2, &argv[2]);
		ret = ww_playlist_create(pl, uuid, argc > 2 ? name : NULL);
		break;
	case PL_NAME:
		join_args(name, sizeof(name), argc - 2, &argv[2]);
		ret = ww_playlist_set_name(pl, name);
		break;
	case PL_ADD:
		ret = ww_playlist_add(pl, argv[2]);
		if (ret >= 0) {
			shell_print(sh, "Added %d track(s)", ret);
		}
		break;
	case PL_RM:
		ret = ww_playlist_remove_track(pl, strtoul(argv[2], NULL, 0) - 1);
		break;
	case PL_DEL:
		ret = ww_playlist_delete(uuid);
		break;
	}
	if (ret < 0) {
		shell_error(sh, "Failed: %d", ret);
	}

out:
	ww_playlist_scratch_unlock(op != PL_SHOW && ret >= 0);

	return ret < 0 ? ret : 0;
}

#define PL_CMD(_name, _op)                                                                         \
	static int cmd_pl_##_name(const struct shell *sh, size_t argc, char **argv)                \
	{                                                                                          \
		return pl_edit(sh, _op, argc, argv);                                               \
	}

PL_CMD(show, PL_SHOW)
PL_CMD(create, PL_CREATE)
PL_CMD(name, PL_NAME)
PL_CMD(add, PL_ADD)
PL_CMD(rm, PL_RM)
PL_CMD(del, PL_DEL)

static int cmd_ls(const struct shell *sh, size_t argc, char **argv)
{
	char path[WW_PATH_MAX];
	struct fs_dir_t dir;
	struct fs_dirent ent;
	int ret;

	ret = ww_storage_path(path, sizeof(path), argc > 1 ? argv[1] : "/");
	if (ret) {
		return ret;
	}

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, path);
	if (ret) {
		shell_error(sh, "Cannot open %s: %d", path, ret);
		return ret;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
		if (ent.type == FS_DIR_ENTRY_DIR) {
			shell_print(sh, "%10s  %s/", "<dir>", ent.name);
		} else {
			shell_print(sh, "%10zu  %s", ent.size, ent.name);
		}
	}
	fs_closedir(&dir);

	return 0;
}

/* Simulated RFID reader; on hardware the MFRC522 driver posts the same events */

#ifdef CONFIG_WW_TAG_READER_SIM
static int cmd_tag_place(const struct shell *sh, size_t argc, char **argv)
{
	struct ww_event ev = {.type = WW_EV_TAG_PLACED};
	const char *uid = argv[1];
	size_t len = strlen(uid);

	if (len == 0 || len >= WW_UID_STR_LEN || (len % 2) != 0) {
		shell_error(sh, "UID must be 2..%d hex digits", WW_UID_STR_LEN - 1);
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		if (!isxdigit((unsigned char)uid[i])) {
			shell_error(sh, "UID must be hex");
			return -EINVAL;
		}
		ev.tag.uid[i] = tolower((unsigned char)uid[i]);
	}

	return ww_event_post(&ev);
}

static int cmd_tag_remove(const struct shell *sh, size_t argc, char **argv)
{
	struct ww_event ev = {.type = WW_EV_TAG_REMOVED};

	return ww_event_post(&ev);
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_tag,
	SHELL_CMD_ARG(place, NULL, "Place a tag: place <uid-hex>", cmd_tag_place, 2, 0),
	SHELL_CMD_ARG(remove, NULL, "Remove the tag", cmd_tag_remove, 1, 0),
	SHELL_SUBCMD_SET_END);
#endif

/*
 * Simulated button and volume pot: driven through the GPIO and ADC emulators,
 * so the gpio-keys / analog-axis -> input -> controller paths are exercised.
 */

#ifdef CONFIG_WW_KEYS_SIM
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_NODELABEL(button), gpios);

#define POT_AXIS DT_CHILD(DT_NODELABEL(volume_pot), axis)
#define POT_ADC  DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(POT_AXIS))
#define POT_CHAN DT_IO_CHANNELS_INPUT(POT_AXIS)
#define POT_MV   DT_PROP(DT_IO_CHANNELS_CTLR(POT_AXIS), ref_internal_mv)

static bool button_held;

static void release_button(struct k_work *work)
{
	gpio_emul_input_set(button.port, button.pin, 0);
	button_held = false;
}

static K_WORK_DELAYABLE_DEFINE(release_work, release_button);

static int cmd_button(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t hold_ms = argc > 1 ? strtoul(argv[1], NULL, 0) : 100;

	if (button_held) {
		shell_error(sh, "The button is still held");
		return -EBUSY;
	}

	button_held = true;
	gpio_emul_input_set(button.port, button.pin, 1);
	k_work_schedule(&release_work, K_MSEC(hold_ms));

	return 0;
}

static int cmd_pot(const struct shell *sh, size_t argc, char **argv)
{
	int percent = CLAMP(strtol(argv[1], NULL, 0), 0, 100);

	return adc_emul_const_value_set(POT_ADC, POT_CHAN, POT_MV * percent / 100);
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pl,
	SHELL_CMD_ARG(list, NULL, "List playlists", cmd_pl_list, 1, 0),
	SHELL_CMD_ARG(show, NULL, "Show tracks: show <uuid>", cmd_pl_show, 2, 0),
	SHELL_CMD_ARG(create, NULL, "Create: create <uuid> [name]", cmd_pl_create, 2, 8),
	SHELL_CMD_ARG(name, NULL, "Rename: name <uuid> <name>", cmd_pl_name, 3, 8),
	SHELL_CMD_ARG(add, NULL, "Add an mp3 or a folder: add <uuid> </Music/...>", cmd_pl_add,
		      3, 0),
	SHELL_CMD_ARG(rm, NULL, "Remove track: rm <uuid> <nr>", cmd_pl_rm, 3, 0),
	SHELL_CMD_ARG(del, NULL, "Delete playlist: del <uuid>", cmd_pl_del, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ww,
	SHELL_CMD_ARG(status, NULL, "Show the box state", cmd_status, 1, 0),
	SHELL_CMD_ARG(next, NULL, "Next track", cmd_next, 1, 0),
	SHELL_CMD_ARG(reset, NULL, "Restart the playlist from the first track", cmd_reset, 1, 0),
	SHELL_CMD_ARG(reset-all, NULL, "Forget the positions of all playlists", cmd_reset_all, 1,
		      0),
	SHELL_CMD_ARG(vol, NULL, "Get/set volume: vol [0..max] (the pot overrides it)", cmd_vol,
		      1, 1),
	SHELL_CMD_ARG(seek, NULL, "Seek in the track: seek <seconds>", cmd_seek, 2, 0),
	SHELL_CMD_ARG(track, NULL, "Jump to track: track <nr>", cmd_track, 2, 0),
	SHELL_CMD(pl, &sub_pl, "Playlists", NULL),
	SHELL_CMD_ARG(ls, NULL, "List SD card: ls [/path]", cmd_ls, 1, 1),
#ifdef CONFIG_WW_TAG_READER_SIM
	SHELL_CMD(tag, &sub_tag, "Simulated RFID reader", NULL),
#endif
#ifdef CONFIG_WW_KEYS_SIM
	SHELL_CMD_ARG(button, NULL,
		      "Press the button: button [hold ms, default 100]; "
		      "<1 s next track, >=1 s restart playlist, >=10 s reset all",
		      cmd_button, 1, 1),
	SHELL_CMD_ARG(pot, NULL, "Turn the volume pot: pot <0..100>", cmd_pot, 2, 0),
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ww, &sub_ww, "WirbelWind music box", NULL);
