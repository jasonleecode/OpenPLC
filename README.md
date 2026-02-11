# OpenPLC
OpenPLC with WASM

这是一个全新的OpenPLC项目，基于wasm技术实现的软PLC。使用wasm的好处是，一次编译，到处运行，且不限于PLC程序，配合DDS技术，可以做到分布式计算。


editor是基于Qt6编写的梯形图编程工具，能够进行图形化的PLC编程，编译后可以通过网络将PLC程序下载到运行着runtime的PLC设备中；
runtime是一个基于rt-thread系统的PLC core，可以加载上位机editor下载的PLC程序。
支持在线调试。
支持IEC 61131标准，PLC编译器选用

