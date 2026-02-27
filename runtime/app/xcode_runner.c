/*
 * xcode_runner.c — TiZi XCODE 模式 WAMR 运行器
 *
 * 职责：
 *   在 XCODE 模式下，Runtime A 不再直接调用 B 区原生函数，
 *   而是通过 WAMR (WebAssembly Micro Runtime) 加载并执行 B 区的 .wasm 字节码。
 *
 * B 区内存布局（XCODE 模式）：
 *   Flash B (0x00004000): [4字节魔数 XCODE_MAGIC][4字节wasm大小][wasm字节码...]
 *
 * .wasm 导出接口（由 editor 的 plc_wasm_main.c 生成）：
 *   plc_init()       — PLC 初始化，调用一次
 *   plc_run(uint32)  — PLC 扫描周期，参数为当前时间(ms)
 *
 * 编译条件：仅在 MODE=XCODE 时编译（Makefile 控制）
 */

#ifdef XCODE_MODE

#include "shared_interface.h"
#include "wasm_export.h"   /* WAMR 公共 API (core/iwasm/include/) */

/* -----------------------------------------------------------------------
 * XCODE B 区头部魔数（与 NCC 的 USER_LOGIC_MAGIC 区分）
 * -----------------------------------------------------------------------*/
#define XCODE_WASM_MAGIC    0x57415300U  /* "WAS\0" */

/* B 区头部结构（XCODE 模式下 Flash B 起始处的布局）*/
typedef struct {
    uint32_t magic;       /* XCODE_WASM_MAGIC */
    uint32_t wasm_size;   /* .wasm 字节码大小（字节）*/
    /* 紧随其后：wasm_size 字节的 .wasm 内容 */
} XcodeHeader_t;

/* -----------------------------------------------------------------------
 * WAMR 运行时状态
 * -----------------------------------------------------------------------*/
#define WAMR_HEAP_SIZE      (4u * 1024u)  /* WAMR 全局堆：4KB（RAM B 区）*/
#define WASM_STACK_SIZE     (2u * 1024u)  /* WASM 解释栈：2KB */

static uint8_t           s_wamr_heap[WAMR_HEAP_SIZE];
static wasm_module_t     s_module     = NULL;
static wasm_module_inst_t s_inst      = NULL;
static wasm_exec_env_t   s_exec_env  = NULL;
static wasm_function_inst_t s_fn_init = NULL;
static wasm_function_inst_t s_fn_run  = NULL;

static bool s_ready = false;

/* -----------------------------------------------------------------------
 * xcode_runner_init — 初始化 WAMR 并加载 B 区 .wasm
 *
 * 参数：
 *   api  — Runtime A 提供的 System API 函数表（暂留，供未来 host function 使用）
 *
 * 返回：true = 成功，false = B 区没有合法 .wasm 或 WAMR 初始化失败
 * -----------------------------------------------------------------------*/
bool xcode_runner_init(const SystemAPI_t *api)
{
    (void)api;  /* 预留：未来用于注册 WASM host function（如 set_do / get_di）*/

    /* 1. 检查 B 区头部 */
    const XcodeHeader_t *hdr = (const XcodeHeader_t *)USER_FLASH_BASE;
    if (hdr->magic != XCODE_WASM_MAGIC) {
        return false;  /* B 区没有合法 XCODE 固件 */
    }
    if (hdr->wasm_size == 0u || hdr->wasm_size > (USER_FLASH_SIZE - sizeof(XcodeHeader_t))) {
        return false;  /* 大小不合法 */
    }

    const uint8_t *wasm_buf  = (const uint8_t *)(USER_FLASH_BASE + sizeof(XcodeHeader_t));
    uint32_t       wasm_size = hdr->wasm_size;

    /* 2. 初始化 WAMR 运行时 */
    RuntimeInitArgs init_args;
    __builtin_memset(&init_args, 0, sizeof(init_args));
    init_args.mem_alloc_type        = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf  = s_wamr_heap;
    init_args.mem_alloc_option.pool.heap_size = WAMR_HEAP_SIZE;

    if (!wasm_runtime_full_init(&init_args)) {
        return false;
    }

    /* 3. 加载 .wasm 模块 */
    char error_buf[64];
    s_module = wasm_runtime_load(wasm_buf, wasm_size, error_buf, sizeof(error_buf));
    if (!s_module) {
        return false;
    }

    /* 4. 实例化 */
    s_inst = wasm_runtime_instantiate(s_module, WASM_STACK_SIZE, 0,
                                      error_buf, sizeof(error_buf));
    if (!s_inst) {
        wasm_runtime_unload(s_module);
        return false;
    }

    /* 5. 查找导出函数 */
    s_fn_init = wasm_runtime_lookup_function(s_inst, "plc_init", NULL);
    s_fn_run  = wasm_runtime_lookup_function(s_inst, "plc_run",  NULL);
    if (!s_fn_init || !s_fn_run) {
        wasm_runtime_deinstantiate(s_inst);
        wasm_runtime_unload(s_module);
        return false;
    }

    /* 6. 创建执行环境 */
    s_exec_env = wasm_runtime_create_exec_env(s_inst, WASM_STACK_SIZE);
    if (!s_exec_env) {
        wasm_runtime_deinstantiate(s_inst);
        wasm_runtime_unload(s_module);
        return false;
    }

    /* 7. 调用 plc_init() */
    if (!wasm_runtime_call_wasm(s_exec_env, s_fn_init, 0, NULL)) {
        wasm_runtime_destroy_exec_env(s_exec_env);
        wasm_runtime_deinstantiate(s_inst);
        wasm_runtime_unload(s_module);
        return false;
    }

    s_ready = true;
    return true;
}

/* -----------------------------------------------------------------------
 * xcode_runner_loop — 每个扫描周期调用一次
 *
 * 参数：
 *   tick_ms — 当前系统时间（毫秒），传给 WASM 的 plc_run(ms)
 * -----------------------------------------------------------------------*/
void xcode_runner_loop(uint32_t tick_ms)
{
    if (!s_ready) return;

    /* 调用 plc_run(uint32_t ms) */
    uint32_t argv[1] = { tick_ms };
    wasm_runtime_call_wasm(s_exec_env, s_fn_run, 1, argv);
}

#endif /* XCODE_MODE */
