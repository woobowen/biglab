/* 标准输出和报错机制 */

#include "mod.h"
#include <stdarg.h>

static char digits[] = "0123456789abcdef";

/* 如果发生panic, UART的停止标志 */
volatile int panicked = 0;

/* printf的自旋锁 */
static spinlock_t print_lk;//全局锁

/* 初始化uart + 初始化printf锁 */
void print_init(void)
{
    uart_init();
    cons_init();
    spinlock_init(&print_lk, "printf");
}

/* %d %p */
static void printint(int xx, int base, int sign)
{
    char buf[16];
    int i;
    uint32 x;

    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    i = 0;
    do
    {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0)
        uart_putc_sync(buf[i]);
}

/* %x */
static void printptr(uint64 x)
{
    uart_putc_sync('0');
    uart_putc_sync('x');
    for (int i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
        uart_putc_sync(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

/*
    标准化输出, 需要支持:
    1. %d (32位有符号数,以10进制输出)
    2. %p (64位无符号数,以0x开头的16进制输出)
    3. %x (32位无符号数,以16进制输出)
    4. %c (单个字符)
    5. %s (字符串)
    提示: stdarg.h中的va_list中包括你需要的参数地址
*/
void printf(const char *fmt, ...)
{
    va_list ap; //ap用于遍历可变参数
    const char *p;//p用于遍历格式字符串
    char c;
    char *s;

    va_start(ap, fmt); // 使ap指向第一个可变参数的地址

    //加锁
    spinlock_acquire(&print_lk);

    //如果已经panic, 则不再输出
    if(panicked){
        spinlock_release(&print_lk); 
        va_end(ap);
        return;
    }

    //解析格式字符串 fmt，遇到 % 就从可变参数中取出对应值并打印
    for(p = fmt;*p;p++){
        //不是格式化字符, 直接输出
        if(*p!='%'){
            uart_putc_sync(*p);
            continue;
        }
        p++;
        switch(*p){
            case 'd':
                printint(va_arg(ap,int),10,1);
                break;
            case 'p':
                printptr(va_arg(ap,uint64));
                break;
            case 'x':
                printint(va_arg(ap,uint32),16,0);
                break;
            case 'c':
                c=va_arg(ap,int); // char会被提升为int
                uart_putc_sync(c);
                break;
            case 's':
                s=va_arg(ap,char*);
                if(s==0)
                    s="(null)";
                while(*s!='\0'){
                    uart_putc_sync(*s);
                    s++;
                }
                break;
            default:
                uart_putc_sync('%');
                uart_putc_sync(*p);
                break;
        }
    }

    //解锁
    spinlock_release(&print_lk);

    //清理ap
    va_end(ap);
}




/* 报错并终止输出 */
void panic(const char *s)
{
    push_off(); //关中断

    if(!panicked){ //避免不同核重复调用
        printf("panic! %s\n", s?s:"<null>"); //报错
        panicked = 1;
    }

    while (1){
        asm volatile("wfi"); //安全停机
    }
}

/* 如果不满足条件, 则调用panic */
void assert(bool condition, const char *warning)
{
     if (!condition) {
        panic(warning);
    }
}
