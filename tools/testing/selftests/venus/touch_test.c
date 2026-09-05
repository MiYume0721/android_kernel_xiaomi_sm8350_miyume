// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FTS_XIAOMI_TOUCHFEATURE
#define FTS_FOD_AREA_REPORT
#define CONFIG_SECURE_TOUCH
#define CONFIG_FTS_POWERSUPPLY_CB
#define CONFIG_FTS_BOOST
#define TOUCH_THP_SUPPORT
#define OK 0
#define EVENT_INPUT 0
#define FOD_LONGPRESS_EVENT 1
#define FOD_SINGLETAP_EVENT 2
#define SCAN_MODE_ACTIVE 1
#define SCAN_MODE_LOW_POWER 2
#define XIAOMI_TOUCH_RESUME 0
#define XIAOMI_TOUCH_SUSPEND 1
#define NOTIFY_OK 0
#define MI_DISPLAY_PRIMARY 0
#define MI_DISP_DPMS_EARLY_EVENT 1
#define MI_DISP_DPMS_EVENT 2
#define MI_DISP_DPMS_POWERDOWN 0
#define MI_DISP_DPMS_ON 1
#define MI_DISP_DPMS_LP1 2
#define MI_DISP_DPMS_LP2 3
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define logError(...) ((void)0)
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
typedef unsigned char u8;
struct work_struct { int dummy; };
struct notifier_block { int dummy; };
struct mi_disp_notifier { int disp_id; void *data; };
struct test_queue { struct work_struct *pending; };
struct fts_ts_info {
	struct work_struct resume_work, suspend_work, switch_mode_work;
	struct notifier_block notifier;
	struct test_queue *event_wq;
	bool aod_status_from_display;
	bool power_state_valid, sensor_sleep, fod_pressed, probe_ok;
	bool palm_sensor_switch, palm_sensor_changed, enable_touch_raw;
	int resume_bit, charging_status, sleep_finger, gesture_enabled, fod_mutex;
	int aod_status, nonui_status, fod_status, fod_icon_status;
};
static struct fts_ts_info info;
static struct fts_ts_info *fts_info = &info;
static int resets, mode_calls, fail_mode, fail_reset, irq_enable, irq_disable;
static int scans, scan_mode, commands;
static u8 last_command[6];
static int fts_disableInterrupt(void) { irq_disable++; return 0; }
static int fts_enableInterrupt(void) { irq_enable++; return 0; }
static int fts_system_reset(void) { resets++; return fail_reset; }
static int fts_mode_handler(struct fts_ts_info *p, int force)
{
	mode_calls++;
	return fail_mode;
}
static void fts_secure_stop(struct fts_ts_info *p, bool stop) {}
#ifdef CONFIG_FACTORY_BUILD
static int fts_enable_reg(struct fts_ts_info *p, bool on) { return 0; }
#endif
static void release_all_touches(struct fts_ts_info *p) {}
static void fts_write_charge_status(int s) {}
static void fts_palm_sensor_cmd(int s) {}
static void update_palm_sensor_value(int s) {}
static void fts_enable_thp_cmd(int s) {}
static void msleep(int ms) {}
static void xiaomi_touch_set_suspend_state(int s) {}
static void lpm_disable_for_dev(bool b, int event) {}
static int fts_write_dma_safe(u8 *cmd, size_t len)
{
	assert(len == sizeof(last_command));
	memcpy(last_command, cmd, len);
	commands++;
	return 0;
}
static int setScanMode(int mode, int value) { scan_mode = mode; scans++; return 0; }
static u8 fts_need_enter_lp_mode(void);
static void fts_resume_work(struct work_struct *);
static void fts_suspend_work(struct work_struct *);
static void fts_switch_mode_work(struct work_struct *);
static int fts_drm_state_chg_callback(struct notifier_block *, unsigned long, void *);
static struct test_queue event_queue;

static bool queue_work(struct test_queue *q, struct work_struct *work)
{
	if (q->pending == work)
		return false;
	assert(q->pending == NULL);
	q->pending = work;
	return true;
}

static void flush_workqueue(struct test_queue *q)
{
	struct work_struct *work = q->pending;

	q->pending = NULL;
	if (work == &info.resume_work)
		fts_resume_work(work);
	else if (work == &info.suspend_work)
		fts_suspend_work(work);
	else
		assert(!work);
}

static void schedule_work(struct work_struct *work)
{
	assert(work == &info.switch_mode_work);
	fts_switch_mode_work(work);
}

static void notify_display(int event, unsigned int blank)
{
	struct mi_disp_notifier notification = { MI_DISPLAY_PRIMARY, &blank };

	assert(fts_drm_state_chg_callback(&info.notifier, event, &notification) == NOTIFY_OK);
}

int main(void)
{
	int before, before_commands;

	info.fod_status = -1;
	info.resume_bit = 1;
	/* Unknown initial state must not suppress the first transition. */
	fts_resume_work(&info.resume_work);
	assert(resets == 1 && mode_calls == 1 && info.power_state_valid);
	fts_resume_work(&info.resume_work);
	assert(resets == 1 && mode_calls == 1);
	fts_suspend_work(&info.suspend_work);
	assert(info.sensor_sleep && !info.resume_bit && info.power_state_valid);
	before = mode_calls;
	fts_suspend_work(&info.suspend_work);
	assert(mode_calls == before);

	/* Mode updates in an already-suspended state must bypass power dedup. */
	info.aod_status = 1;
	fts_switch_mode_work(&info.switch_mode_work);
	assert(last_command[5] == FOD_SINGLETAP_EVENT && scan_mode == SCAN_MODE_LOW_POWER);
	before_commands = commands;
	fts_suspend_work(&info.suspend_work);
	assert(mode_calls == before && commands == before_commands);
	info.gesture_enabled = 1;
	fts_switch_mode_work(&info.switch_mode_work);
	assert(last_command[2] == 0x20 && last_command[5] == FOD_SINGLETAP_EVENT);
	info.nonui_status = 1;
	fts_switch_mode_work(&info.switch_mode_work);
	assert(last_command[2] == 0x20 && last_command[5] == 0);
	info.gesture_enabled = 0;
	fts_switch_mode_work(&info.switch_mode_work);
	assert(scan_mode == SCAN_MODE_ACTIVE);
	info.nonui_status = info.aod_status = 0;
	info.fod_status = 1;
	fts_switch_mode_work(&info.switch_mode_work);
	assert(last_command[5] == FOD_LONGPRESS_EVENT);

	/* Failure does not make the state valid; the same request may retry. */
	fail_reset = -1;
	fts_resume_work(&info.resume_work);
	assert(!info.power_state_valid);
	before = resets;
	fail_reset = 0;
	fts_resume_work(&info.resume_work);
	assert(resets == before + 1 && info.power_state_valid);
	fail_mode = -1;
	fts_suspend_work(&info.suspend_work);
	assert(!info.power_state_valid);
	before = mode_calls;
	fail_mode = 0;
	fts_suspend_work(&info.suspend_work);
	assert(mode_calls == before + 1 && info.power_state_valid);
	before = resets;
	info.fod_pressed = true;
	fts_resume_work(&info.resume_work);
#ifndef CONFIG_FACTORY_BUILD
	assert(resets == before); /* Retain the pressed-FOD reset exclusion. */
#else
	assert(resets == before + 1);
#endif
	before = resets;
	fts_resume_work(&info.resume_work);
	assert(resets == before);
	before_commands = commands;
	fts_switch_mode_work(&info.switch_mode_work);
	assert(commands == before_commands); /* Awake mode-update exclusion. */

	/* ON arriving while OFF is queued must not be discarded as redundant. */
	info.event_wq = &event_queue;
	info.fod_pressed = false;
	notify_display(MI_DISP_DPMS_EARLY_EVENT, MI_DISP_DPMS_POWERDOWN);
	assert(event_queue.pending == &info.suspend_work && !info.sensor_sleep);
	notify_display(MI_DISP_DPMS_EVENT, MI_DISP_DPMS_ON);
	flush_workqueue(&event_queue);
	assert(!info.sensor_sleep && info.power_state_valid);
	/* The reverse ordering must likewise retain the most recent request. */
	fts_suspend_work(&info.suspend_work);
	notify_display(MI_DISP_DPMS_EVENT, MI_DISP_DPMS_ON);
	notify_display(MI_DISP_DPMS_EARLY_EVENT, MI_DISP_DPMS_POWERDOWN);
	flush_workqueue(&event_queue);
	assert(info.sensor_sleep && info.power_state_valid);
	info.aod_status = 0;
	notify_display(MI_DISP_DPMS_EARLY_EVENT, MI_DISP_DPMS_LP1);
	flush_workqueue(&event_queue);
	assert(info.aod_status == 1 && info.aod_status_from_display);
	assert(last_command[5] & FOD_SINGLETAP_EVENT);
	before = mode_calls;
	notify_display(MI_DISP_DPMS_EARLY_EVENT, MI_DISP_DPMS_LP2);
	flush_workqueue(&event_queue);
	assert(mode_calls == before);
	notify_display(MI_DISP_DPMS_EVENT, MI_DISP_DPMS_ON);
	flush_workqueue(&event_queue);
	assert(!info.aod_status && !info.aod_status_from_display);
	assert(scans > 0 && irq_enable > 0 && irq_disable > 0);
	puts("PASS touch: duplicates, failure retries, queued OFF/ON order, LP1/LP2/AOD/double-tap/non-UI/FOD");
	return 0;
}
