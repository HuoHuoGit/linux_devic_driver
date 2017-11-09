#include <linux/init.h>
#include <linux/moudle.h>
#include <linux/proc_fs.h>
MODULE_LICENSE( "Dual BSD/GPL" );

char * g_s = "This is hello proc read";

int hello_read_proc( char *page, char **start, off_t offset, int count, int *eof, void *data )
{
	int ret;

	ret = sprintf( page, g_s );

	return ret;
}

static int hello_init( void )
{
	printk( KERN_ALERT "Hello, world!" );

	create_proc_read_entry( "helloread", 0, NULL, hello_read_proc, NULL );

	return 0;
}

static void hello_exit( void )
{
	printk( KERN_ALERT "Goodbye" );

	remove_proc_entry( "scullmem", NULL );
}

module_init( hello_init );
module_exit( hello_exit );
