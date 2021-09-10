# RDMA-middleware

## 1. 代码结构

### 1.1 redis目录

`redis`目录为修改后的`redis`源码，主要修改如下：

1. 在`src`子目录下添加了`copy.h`和`copy.c`，该部分为复制过程的主要逻辑
2. 在`deps`子目录下添加了`midd`子目录，该目录为`middleware`依赖，主要包括头文件和静态链接库
3. 修改了`src`子目录下的`server.c`，主要调用`copy.c`中的逻辑
4. 修改了`src`子目录下的`Makefile`，添加复制中间件的编译选项

### 1.2  middleware目录

middleware`目录为复制中间件的源码

1. `middleware/bin` 包含测试时的可执行文件和相应的动态库

2. `middleware/include`包含项目中用到的各种头文件

3. `middleware/src` 包含RDMA链接建立，RDMA-rpc实现，zero-copy 的环形缓冲区实现等，下面是核心的实现文件

   1. mid_rdma_task.c: 包含不同种类的 RDMA-rpc 任务
   2. mid_client_work.c: 高层的任务种类实现
   3. mid_rdma_connection.c: 链接管理器
   4. mid_rdma_transport.c: 底层 RDMA-api 封装
   5. log_buffer.c: zero-copy 环形缓冲区实现

   

## 2. 额外的依赖

在运行代码之前， 需要保证有如下的依赖安装。

- numactl-devel
- ndctl
- RDMA driver (需要对应的包, 比如 libibverbs/librdmacm/etc)

## 3.  编译和运行

为了运行本项目，这个项目必须被完全编译. 项目需要软件和硬件的相应配置，比如 RDMA 网络配置等。

### **3.1 运行环境设置**

​    首先，需要根据运行时环境配置 config.xml 文件

```bash
cd bin
vim config.xml
```

   在 config.xml 中，下面的信息需要在所有的节点被配置;  节点id顺序从小到大排列，id最小的节点为头节点，id最大的为尾节点

```xml
<?xml version="1.0" encoding="UTF-8"?>
<dhmp_config>
	<!-- 日至等级 -->
	<client>
		<log_level>6</log_level>
	</client>

	<!-- 服务器列表 -->
	<!-- 该顺序也为链式复制的顺序 -->
	<server id="1">
		<nic_name>ib0</nic_name>	<!-- 网卡名称 -->
		<addr>10.10.10.1</addr>		<!-- 网卡ip -->
		<port>39300</port>			<!-- 端口 -->
		<dram_node>0</dram_node>	<!-- dram内存对应的numa节点 -->
		<nvm_node>4</nvm_node>		<!-- nvm内存对应的numa节点 -->
	</server>

	<server id="2">
		<nic_name>ib0</nic_name>
		<addr>10.10.10.2</addr>
		<port>39330</port>
		<dram_node>0</dram_node>
		<nvm_node>4</nvm_node>
	</server>

	<server id="3">
		<nic_name>ib0</nic_name>
		<addr>10.10.10.3</addr>
		<port>39330</port>
		<dram_node>0</dram_node>
		<nvm_node>4</nvm_node>
	</server>
</dhmp_config>
```

### 3.2  Redis环境下的编译和运行

1. 将`middleware`中的头文件和编译生成的静态链接库复制到`redis/deps/midd`目录下
2. 在`redis`目录下执行`make MIDD=yes`完成Redis的编译
3. 配置`redis/config.xml`文件，配置过程如下文
4. 针对每个节点，在`redis`下执行`./src/redis-server`启用Redis服务器
5. 在`redis`下执行`./src/redis-cli`启动客户端来完成测试

### **3.3    测试条件下的编译和运行**

​    如果不需要 Redis 环境，可以使用我们预先提供好的脚本对项目进行编译，生成可供测试的二进制文件。

```shell
./rebuild.sh	# 生成测试应用程序和库
./bin/midd	# 执行测试环境下的二进制文件
```

