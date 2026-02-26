让整个编辑器支持plugins扩展，后续可以通过插件的形式扩展功能
现在Library，里面各种组件，也使用plugin的形式


对不同PLC硬件平台，仿照InfoTeam，采用驱动安装的形式

drivers挪到editor/tests下

在editor里点击PLC->online时，通过检查PLC里固件的md5与当前固件的md5，如果发现不匹配则提示是否下载更新

将iec2c工具挪到editor/src/editor/tools下

参考OpenPCS配置连接和下载程序的逻辑

