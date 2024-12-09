# 自动测试
chmod +x test.sh && ./test.sh
## fail
![alt text](assets/image.png)


## debug
####
只有4个文件夹重挂载仍然存在
![alt text](assets/image-2.png)
####
文件夹递归包含重挂载仍然存在
![alt text](assets/image-3.png)
####
一旦有一个文件存在，在卸载时都不能正确写入磁盘
![alt text](assets/image-4.png)