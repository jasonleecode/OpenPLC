# OpenPLC-X
OpenPLC-X项目，支持IEC 61131-3标准，PLC编译器选用matiec。支持LD和ST等PLC编程语言。
同类的项目有Bememiz，但是这套代码组织很乱，历史包袱很重，基于Python，没有runtime相关的实现。


editor是基于Qt6编写的梯形图编程工具，能够进行图形化的PLC编程，编译后可以通过网络将PLC程序下载到运行着runtime的PLC设备中；支持LD和ST等PLC编程语言；采用MVC分层；

runtime是一个基于rt-thread系统的PLC core，可以加载上位机editor下载的PLC程序。PLC硬件平台暂时使用LPC824处理器，由于资源有限，暂时不使用OS，使用裸机程序先把整体功能跑通；Flash分为A区和B区，A区存储Runtime，B区存储User Logic；A区通常只烧录一次，通过ETH或者Serial与上位机编程工具建立连接来更新B区的用户PLC程序；支持在线调试。


