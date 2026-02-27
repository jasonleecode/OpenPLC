一个非常经典且可行的嵌入式架构设计，通常被称为**“固件与应用分离”或“可加载模块”设计。在工业控制领域（如PLC）中，这正是Runtime（运行时系统/解释器）与User Logic（用户逻辑）**分离的核心实现方式。

要实现你的需求（A保持不变，只更新B，且A能调用B），核心在于解决两个问题：

内存分区：物理上将A和B隔离开。

链接与接口：A如何找到B的函数入口（因为B重新编译后，内部函数地址可能会变，必须有一个固定的入口）。

以下是基于STM32实现该机制的详细步骤：

1. 核心设计思路：函数跳转表 (Jump Table)
由于B的代码更新后，其内部函数 func_1, func_2 的地址会发生变化。A不能直接调用这些变动的地址。
解决方案：在B的固定起始地址（如Flash的某个扇区首地址）放置一个结构体，这个结构体里存放了B中所有对外公开的函数指针。A只需要访问这个固定的地址，读取指针即可调用。

2. 具体实现步骤
假设我们使用的是STM32F4（Flash 0x08000000起），我们将Flash和RAM分为两部分：

A区域 (PLC Runtime): Flash 0x08000000 (大小64KB) | RAM 0x20000000 (大小64KB)

B区域 (用户PLC程序): Flash 0x08010000 (剩余部分) | RAM 0x20010000 (剩余部分)

第一步：定义共享接口 (Shared Header)
创建一个头文件，A和B都要引用它。这定义了A和B通信的“协议”。

// shared_interface.h

// 定义函数指针类型
typedef void (*pSetupFunc)(void);
typedef void (*pLoopFunc)(void);
// 比如PLC的输入输出更新函数
typedef void (*pUpdateIOFunc)(uint8_t* inputs, uint8_t* outputs);

// 核心跳转表结构体
typedef struct {
    uint32_t magic_code; // 用于校验B区是否有有效程序
    uint32_t version;    // 版本号
    pSetupFunc setup;    // 初始化函数
    pLoopFunc loop;      // 周期运行函数
    pUpdateIOFunc io_update;
} UserLogic_Interface_t;

// 假设B区的固定起始地址是 0x08010000
#define USER_LOGIC_ADDRESS  0x08010000

第二步：编写 B 部分 (用户逻辑/PLC程序)
B部分的关键是：必须把 UserLogic_Interface_t 这个结构体实例放到 0x08010000 这个绝对地址上。

C代码 (main_b.c):
#include "shared_interface.h"

// B的具体函数实现
void User_Setup(void) {
    // 用户初始化代码
}

void User_Loop(void) {
    // 用户PLC梯形图转换过来的逻辑
}

void User_IO(uint8_t* in, uint8_t* out) {
    // 简单的逻辑示例
    if (in[0] == 1) out[0] = 1;
    else out[0] = 0;
}

// 定义导出表，并使用编译器属性将其放入特定段 ".user_section"
// __attribute__((section(".user_section"))) 确保连接器把它放在最前面
const UserLogic_Interface_t user_api __attribute__((section(".user_section"))) = {
    .magic_code = 0xDEADBEEF,
    .version = 1,
    .setup = User_Setup,
    .loop = User_Loop,
    .io_update = User_IO
};

Linker Script (链接脚本 .ld) 修改:
你需要为B单独做一个工程（或者在同一个工程里配置不同的Target），并修改链接脚本，确保 .user_section 被放在 0x08010000 的最开始。
/* STM32_B_Flash.ld 示例片段 */

MEMORY
{
  /* B区域从 0x08010000 开始 */
  FLASH_B (rx)      : ORIGIN = 0x08010000, LENGTH = 384K
  /* B独占的RAM区域，防止和A冲突 */
  RAM_B   (xrw)     : ORIGIN = 0x20010000, LENGTH = 64K
}

SECTIONS
{
  /* 将接口表固定在Flash B的起始位置 */
  .user_header :
  {
    . = ALIGN(4);
    KEEP(*(.user_section)) /* 对应C代码里的 section */
    . = ALIGN(4);
  } >FLASH_B

  /* 其他代码段 */
  .text :
  {
    *(.text)           
    *(.text*)          
    /* ... */
  } >FLASH_B

  /* ... Data, BSS 等段也必须放在 RAM_B ... */
}

第三步：编写 A 部分 (PLC Runtime/固件)
A部分不需要知道B具体怎么写的，它只需要去固定的地址“找人”。
#include "shared_interface.h"

// 定义一个指针指向B区的起始地址
const UserLogic_Interface_t* user_app = (UserLogic_Interface_t*)USER_LOGIC_ADDRESS;

void main(void) {
    HAL_Init();
    // ... 系统初始化 ...

    // 1. 检查B区是否有有效程序
    if (user_app->magic_code == 0xDEADBEEF) {
        // 调用B的初始化
        user_app->setup();
    } else {
        // B区无程序，进入等待下载模式
    }

    while (1) {
        // PLC 周期扫描
        if (user_app->magic_code == 0xDEADBEEF) {
            
            // 读取物理输入 -> input_buffer
            
            // 调用B的逻辑处理
            user_app->loop();
            // 或者 user_app->io_update(input_buffer, output_buffer);
            
            // 写入物理输出 <- output_buffer
        }
        
        // 处理串口通信、上位机下载命令等
        Handle_Host_Communication();
    }
}

3. 如何实现上位机只下载 B 区
虽然你在描述中说“编译成一个整体”，但要实现“只下载B”，最稳健的工程实践是将A和B拆分为两个独立的Project：

Project A (Firmware): 编译出 firmware.bin。烧录一次，永不（或很少）变动。

Project B (User Logic): 实际上是上位机根据梯形图/ST语言生成的C代码，编译出的 user_logic.bin。

下载流程：

上位机生成 B 的代码。

调用编译器（arm-none-eabi-gcc）使用 B的链接脚本 编译，生成 user.bin。

上位机通过串口/网口连接 A。

A 接收数据，调用内部 FLASH 写入函数（如 HAL_FLASH_Program），将数据写入 0x08010000 开始的区域。

A 写入完成后，软件复位或者重新加载函数指针。

4. 关键注意事项 (坑点预警)
如果你想做PLC，以下几点至关重要：

B 调用 A 的功能 (System API):

通常PLC的用户逻辑也需要调用系统功能（比如Delay, Print, Math）。

方法：可以在 UserLogic_Interface_t 中反向操作。A在调用 setup 时，把一个包含系统函数指针的结构体传给 B。

user_app->setup(&System_API_Table);

这样B就不需要链接A的库，大大减小B的体积（bin文件可能只有几百字节）。

RAM 全局变量初始化:

B程序中如果定义了 int count = 0; (全局变量)，这是一个 .data 或 .bss 段。

标准的C启动代码（Startup file）负责在 main 之前把Flash里的初始值搬运到RAM。

但因为B没有自己的 main（它是被A调用的），A必须负责帮B初始化RAM，或者B的 setup 函数里必须包含一段手动初始化RAM的代码（即把B的 .data 段从Flash B搬运到 RAM B）。

简化版：PLC生成的代码尽量不依赖复杂的C运行时库，或者上位机编译时加上 -fPIC (位置无关代码) 虽在Cortex-M上较麻烦，但通常做法是让B的 setup 自己负责清理自己的变量区。

中断向量表:

如果B需要使用中断，需要重定向向量表（SCB->VTOR）。但作为PLC逻辑，通常建议B不要直接处理中断，中断由A统一管理，以标志位或回调的方式通知B，这样系统更稳定。

总结方案
可以实现。 你需要构建一个**“宿主(A)-插件(B)”**架构。

A 负责硬件驱动、通信、调度，它固化在Flash低地址。

B 是用户逻辑，编译链接到Flash高地址，并在头部保留一张函数表。

更新机制：上位机只通过通信协议发送编译好的B部分二进制流，A接收并写入Flash B区，即可实现逻辑更新而底层不变。