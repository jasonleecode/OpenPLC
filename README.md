# OpenPLC-X
OpenPLC-X项目，支持IEC 61131-3标准，PLC编译器选用matiec。支持LD和ST等PLC编程语言。

<img width="1584" height="1015" alt="1772014885713" src="https://github.com/user-attachments/assets/25d98048-7b1e-4d24-80c2-1759bfe5dbc8" />

支持NCC(Native Code Compiler)模式和XCODE(基于wasm的字节码)模式，在资源有限的mcu平台，建议使用NCC模式，在资源丰富算力较强的平台，推荐使用XCODE模式。


editor是基于Qt6编写的梯形图编程工具，能够进行图形化的PLC编程，编译后可以通过网络将PLC程序下载到运行着runtime的PLC设备中；支持LD和ST等PLC编程语言；采用MVC分层；

runtime是一个基于rt-thread系统的PLC core，可以加载上位机editor下载的PLC程序。PLC硬件平台暂时使用LPC824处理器，由于资源有限，暂时不使用OS，使用裸机程序先把整体功能跑通；Flash分为A区和B区，A区存储Runtime，B区存储User Logic；A区通常只烧录一次，通过ETH或者Serial与上位机编程工具建立连接来更新B区的用户PLC程序；支持在线调试。


