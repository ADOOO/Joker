## Joker

![image-20220902160052965](https://image.perng.cn/image20220902160054.png)

### 版本 1.0

### 工具原理介绍

利用 **Http.sys** 驱动对urlacl进行操作，由于Http.sys是IIS服务器的基础，其优先级高于IIS，不会造成端口bind冲突，达到端口服用效果，由于与受害机对外服务端口相同，可做到极高的隐蔽性。由于其生效后效果很类似于LOL的小丑，故起名为Joker。

### 使用方法 

**此工具的使用前提是 管理员权限 IIS环境**

#### 1. 基于路径进行复用

~~~powershell
Joker.exe "http://*:{PORT}/{PATH}"
~~~

![image-20220902160750422](https://image.perng.cn/image20220902160751.png)

可直接使用**蚁剑**进行连接，配置如下：

![image-20220902161010316](https://image.perng.cn/image20220902161011.png)

​                                                                       *连接密码随便填写*

![image-20220902161210934](https://image.perng.cn/image20220902161212.png)

#### 2.基于HOST进行复用（强烈推荐）

~~~powershell
Joker.exe "http://{HOST}:{PORT}/"
~~~

![image-20220902161434903](https://image.perng.cn/image20220902161436.png)

蚁剑配置如下

![image-20220902161603406](https://image.perng.cn/image20220902161604.png)

![image-20220902161619869](https://image.perng.cn/image20220902161621.png)

**这样，在正常访问80端口的时候为正常业务，在带特殊的头访问80端口的时候则为后门程序。**

 #### 3.Regeorg适配

项目JokerTunnel为对Regeorg适配版本。

![image-20221107111636996](https://image.perng.cn/image-20221107111636996.png)

![image-20221107111724185](https://image.perng.cn/image-20221107111724185.png)

客户端使用Regeorg进行连接即可

### 二次开发 

此项目的handle都集中在handle.h当中，以执行为例，传入参数依次为 **Request的body、RequestBody的长度与响应内容的指针**

![image-20220902162522544](https://image.perng.cn/image20220902162523.png)