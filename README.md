# BL808_MAILBOX
通过核间通信使得d0核心可以使用sd卡    
bouffalo_sdk是博流官方提供的sdk

使用方法：  
1 将BL808_MAILBOX文件夹放入bouffalo_sdk/components中    
2 修改bouffalo_sdk/components目录下的CMakeLists.txt文件，添加一行代码：add_subdirectory(mailbox)    
3 在m0上运行的例程调用mailbox_init()函数初始化mailbox。   
完成以上步骤后，在d0上运行的例程就可以使用open，write，read等函数来操作sd卡上的文件了。
