#include <stdbool.h>
#include <string.h>
#include <board.h>
#include <bl808.h>
#include <bl808_common.h>
#include <ipc_reg.h>
#include "bflb_l1c.h"

#include "mailbox.h"
#include "stdarg.h"

#define DBG_TAG "MBOX"
#include "log.h"

#define MAX_OP_NUM      10
#define MAX_OP_ARGS_NUM 5

//函数对应的标号
#define MBOX_FN_open    0
#define MBOX_FN_read    1
#define MBOX_FN_write   2
#define MBOX_FN_close   3
#define MBOX_FN_lseek   4
#define MBOX_FN_stat    5

//参数类型标签
#define MBOX_EOI        0
#define MBOX_TYPE_NUM   1
#define MBOX_TYPE_PTR   2
#define MBOX_TYPE_STR   3

#define STORE_TEMPLATE(type, ptr, val) \
    do {                               \
        *(type *)ptr = (type)val;      \
        ptr += sizeof(type);           \
    } while (0)

#define STORE32(ptr, val) STORE_TEMPLATE(uint32_t, ptr, val)
#define STORE64(ptr, val) STORE_TEMPLATE(uint64_t, ptr, val)

#define READ_TEMPLATE(type, ptr, val) \
    do {                              \
        val = *(type *)ptr;           \
        ptr += sizeof(type);          \
    } while (0)

#define READ32(ptr, val) READ_TEMPLATE(uint32_t, ptr, val)
#define READ64(ptr, val) READ_TEMPLATE(uint64_t, ptr, val)

#define STORE8(ptr, val) *ptr++ = val
#define READ8(ptr, val)  val = *ptr++;

static uint8_t *heap_start = 0x40000000;

typedef struct mbox_fn {
    uint32_t param_count;
    uint8_t param_types[MAX_OP_ARGS_NUM];
    uint8_t result_type;
    void (*fn)();
} mbox_fn;

//注册用于通信的函数
static mbox_fn mbox_fns[MAX_OP_NUM] = {
    { 3, { MBOX_TYPE_STR, MBOX_TYPE_NUM, MBOX_TYPE_NUM }, MBOX_TYPE_NUM, open },
    { 3, { MBOX_TYPE_NUM, MBOX_TYPE_PTR, MBOX_TYPE_NUM }, MBOX_TYPE_NUM, read },
    { 3, { MBOX_TYPE_NUM, MBOX_TYPE_PTR, MBOX_TYPE_NUM }, MBOX_TYPE_NUM, write },
    { 1, { MBOX_TYPE_NUM }, MBOX_TYPE_NUM, close },
    { 3, { MBOX_TYPE_NUM, MBOX_TYPE_NUM, MBOX_TYPE_NUM }, MBOX_TYPE_NUM, lseek },
    { 2, { MBOX_TYPE_STR, MBOX_TYPE_PTR }, MBOX_TYPE_NUM, stat }
};

//D0
#if defined(CPU_D0)

#include "bflb_uart.h"

#define IPC_SELF_BASE  IPC2_BASE
#define IPC_OTHER_BASE IPC0_BASE
#define IPC_IRQn       IPC_D0_IRQn

extern struct bflb_device_s *console;

int mailbox_send_signal(uint32_t op, uint32_t argc)
{
    BL_WR_REG(IPC_OTHER_BASE, IPC_CPU1_IPC_ILSHR, op);
    BL_WR_REG(IPC_OTHER_BASE, IPC_CPU1_IPC_ILSLR, argc);

    LOG_W("Sent IPC Mailbox Singal: op: %d, argc %d\r\n", op, argc);
    BL_WR_REG(IPC_OTHER_BASE, IPC_CPU1_IPC_ISWR, 1);
    return 0;
}

static inline void store_str(uint8_t **ptr, const char *str)
{
    uint8_t *p = *ptr;
    uint32_t str_len = strlen(str);
    for (uint32_t i = 0; i < str_len; i++) {
        STORE8(p, str[i]);
    }
    *ptr = p;
}

static void create_mbox_head(mbox_fn *cur_fn, uint64_t argv[])
{
    uint8_t *ptr = heap_start, *params;
    uint32_t param_count = cur_fn->param_count;
    uint8_t *param_types = cur_fn->param_types;
    uint32_t i;
    *ptr++ = 1;
    params = ptr;
    ptr += 4 * param_count;
    for (i = 0; i < param_count; i++) {
        switch (param_types[i]) {
            case MBOX_TYPE_NUM:
            case MBOX_TYPE_PTR:
                STORE32(params, argv[i]);
                break;
            case MBOX_TYPE_STR:
                STORE32(params, ptr);
                store_str(&ptr, (const char *)argv[i]);
                break;
        }
    }
}

static inline bool is_print(uint8_t op, uint64_t argv[])
{
    if (op == MBOX_FN_write && (argv[0] == 1 || argv[0] == 2)) {
        return true;
    }
    return false;
}

static void d0_handle_op(uint8_t op, uint64_t argv[], uint32_t *res)
{
    mbox_fn *cur_fn = mbox_fns + op;
    uint8_t *ptr = heap_start;
    if (is_print(op, argv)) {
        bflb_uart_put(console, (uint8_t *)argv[1], argv[2]);
        return;
    }
    bflb_l1c_dcache_clean_all();
    bflb_l1c_dcache_disable();
    create_mbox_head(cur_fn, argv);

    mailbox_send_signal(op, cur_fn->param_count);
    ptr = heap_start;
    while (*ptr != MBOX_EOI) {
        __NOP();
    }
    ptr++;
    switch (cur_fn->result_type) {
        case MBOX_TYPE_NUM:
            READ32(ptr, *res);
            break;
    }
    bflb_l1c_dcache_enable();
}

int open(const char *path, int flags, ...)
{
    uint64_t argv[3];
    uint32_t res;
    va_list ap;

    va_start(ap, flags);

    argv[0] = (uint64_t)path;
    argv[1] = (uint64_t)flags;
    argv[2] = (uint64_t)va_arg(ap, int);
    d0_handle_op(MBOX_FN_open, argv, &res);
    return res;
}

#define FN1(fn_name, ret_type, p1_type)              \
    ret_type fn_name(p1_type p1)                     \
    {                                                \
        uint64_t argv[1];                            \
        uint32_t res;                                \
        argv[0] = (uint64_t)p1;                      \
        d0_handle_op(MBOX_FN_##fn_name, argv, &res); \
        return res;                                  \
    }

#define FN2(fn_name, ret_type, p1_type, p2_type)     \
    ret_type fn_name(p1_type p1, p2_type p2)         \
    {                                                \
        uint64_t argv[2];                            \
        uint32_t res;                                \
        argv[0] = (uint64_t)p1;                      \
        argv[1] = (uint64_t)p2;                      \
        d0_handle_op(MBOX_FN_##fn_name, argv, &res); \
        return res;                                  \
    }

#define FN3(fn_name, ret_type, p1_type, p2_type, p3_type) \
    ret_type fn_name(p1_type p1, p2_type p2, p3_type p3)  \
    {                                                     \
        uint64_t argv[3];                                 \
        uint32_t res;                                     \
        argv[0] = (uint64_t)p1;                           \
        argv[1] = (uint64_t)p2;                           \
        argv[2] = (uint64_t)p3;                           \
        d0_handle_op(MBOX_FN_##fn_name, argv, &res);      \
        return res;                                       \
    }

FN1(close, int, int)
FN2(stat, int, const char *, struct stat *)
FN3(read, ssize_t, int, void *, size_t)
FN3(write, ssize_t, int, const void *, size_t)
FN3(lseek, _off_t, int, _off_t, int)

//M0
#else
#include <reent.h>

#define IPC_SELF_BASE  IPC0_BASE
#define IPC_OTHER_BASE IPC2_BASE
#define IPC_IRQn       IPC_M0_IRQn

typedef void (*GenericFunctionPointer)();

typedef int32_t (*Int32FuncPtr)(GenericFunctionPointer, uint32_t *, uint32_t);

static void fn_call(void (*fn)(), uint32_t argv[], uint32_t argc)
{
    switch (argc) {
        case 1:
            fn(argv[0]);
            break;
        case 2:
            fn(argv[0],argv[1]);
            break;
        case 3:
            fn(argv[0], argv[1], argv[2]);
            break;
    }
}

static volatile Int32FuncPtr Fn_Int32 = (Int32FuncPtr)(uintptr_t)fn_call;

static void m0_handle_op(uint32_t op, uint32_t arg)
{
    uint32_t i;
    uint8_t *ptr = heap_start + 1, *org_ptr = heap_start;
    int res;
    uint32_t argv[MAX_OP_ARGS_NUM];
    mbox_fn *cur_fn = mbox_fns + op;
    bflb_l1c_dcache_disable();
    for (i = 0; i < arg; i++) {
        READ32(ptr, argv[i]);
        LOG_W("argv%d:%d\r\n", i, argv[i]);
    }
    ptr = org_ptr + 1;
    switch (cur_fn->result_type) {
        case MBOX_TYPE_NUM:
            res = Fn_Int32(cur_fn->fn, argv, cur_fn->param_count);
            STORE32(ptr, res);
            break;
    }
    STORE8(org_ptr, MBOX_EOI);
    bflb_l1c_dcache_enable();
}

void IPC_M0_IRQHandler(int irq, void *arg)
{
    uint32_t irqStatus = BL_RD_REG(IPC_SELF_BASE, IPC_CPU0_IPC_IRSRR);
    uint32_t op = BL_RD_REG(IPC_SELF_BASE, IPC_CPU1_IPC_ILSHR);
    uint32_t argc = BL_RD_REG(IPC_SELF_BASE, IPC_CPU1_IPC_ILSLR);

    LOG_W("Got IPC Mailbox op: %d argc: %d\r\n", op, argc);

    m0_handle_op(op, argc);

    LOG_W("Signal Handler Done\r\n");
    //清楚中断位
    BL_WR_REG(IPC_SELF_BASE, IPC_CPU0_IPC_ICR, irqStatus);
}

int mailbox_init()
{
    /* setup the IPC Interupt */
    bflb_irq_attach(IPC_IRQn, IPC_M0_IRQHandler, NULL);
    BL_WR_REG(IPC_SELF_BASE, IPC_CPU0_IPC_IUSR, 0xffffffff);
    bflb_irq_enable(IPC_IRQn);

    return 0;
}

#endif