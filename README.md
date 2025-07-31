# 同轴电缆长度与终端负载检测装置

​	本仓库用于存放2023年电赛B题（同轴电缆长度与终端负载检测装置）的软件程序。



## 一、总体思路

### 1.同轴电缆长度测量

#### 1) 初步思路

​	方波信号输入同轴电缆，会产生反射现象，信号的叠加使我们能够在信号输入端观察到两段式阶梯波。通过使用不同长度的电缆实验可得，阶梯宽度与电缆长度呈现正相关。

​	图为将方波输入电缆后产生的反射信号：阶梯波。其中，输入方波 $f = 250kHz$，低电平 $= 0V$ ，高电平 $= 3.3V$ 。由图可以看出，第一级阶梯波上升电压中点电压 $≈ 840mV$，第二级阶梯波上升电压中点电压 $≈ 2.6V$。

<img src="https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507290000083.png" alt="阶梯波示波器实拍图" style="zoom:50%;" />

​	我们使用STM32向同轴电缆输入连续方波信号，将所得反射信号（阶梯波）输入**同相输入端电压（电压阈值）分别设为两级阶梯上升电压中点**的TLV3501比较器，得到两路下降沿A、B。

> TLV3501比较器的反相输入端（IN-）电压高于同相输入端（IN+）电压时，输出为低（GND）；反之，当IN+电压高于IN-电压时，输出为高（VCC，3.3V）。

​	将两路下降沿输入FPGA，4个相位时钟两两差开90度分别给计数器计数。将时钟倍频至250M，每个计数分辨率就是4ns。把四个相位的计数器的结果相加，得到精度为1ns的计数器结果。由于题目所要求测量的最长电缆长度为20m，而20m的电缆反射产生的阶梯波宽度约为200ns，四个相位的计数器的结果相加的和不会超过255，因而选择通过8个IO口直接并行输出。

​	使用STM32的8个IO读取FPGA的输出，二进制转十进制即得两路下降沿间隔时间。更换不同长度的电缆，记录电缆长度与对应时间差数据，根据散点拟合 “电缆长度y <---> 阶梯宽度（时间差）x” 对应关系（近似为一次函数），写入STM32程序中。这样，STM32通过拟合出的对应关系，根据时间差推算出当前电缆长度，显示在OLED上供查看。



##### ① 系统框图

​	系统框图如下。其中，STM32F103C8T6 + 50Ω是为了模拟具有50Ω输出电阻的信号发生器。阶梯波可从50Ω与SMA母座之间由示波器测出。

![image-20250715165522505](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507151658709.png)

##### ② 仿真电路图

​	仿真电路图如下。

> 注：LMV358作为跟随器，其实可以去掉。图中最左侧的两个可变电阻器起分压作用，参数可调。本图中，输入LMV358的3脚和5脚的电压应分别调节为两级阶梯波上升电压中点。`输出下降沿A`、`输出下降沿B`将被输入FPGA。）

![image-20250728235437166](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507290000085.png)

​	为模拟幅度范围为0-3.3V的阶梯波，信号发生器参数设置如下。

<img src="https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507290000086.png" alt="image-20250728225331930" style="zoom:67%;" />

##### ③ 仿真结果

​	仿真结果如下。受仿真条件所限，仅模拟出了下降部分的阶梯。将下图水平翻转，即为上升部分的情况。

>注：仿真无法完全模拟实际中阶梯波的波形及幅度，由图可见仿真结果并不稳定。实物中，由于R7和R8的存在，大部分电压阈值附近的震荡已经被消除。

![正确旧仿真结果图](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507290000087.png)

​	水平翻转后，选取较好的一段波形进行分析。如下：

![正确旧仿真结果图（分析）](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507290000088.png)

##### ④ 时序图

​	时序图如下。

![202507151940528](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507271626471.png)

#### 2) 问题

i. 测试发现，STM32的方波输出IO连接洞洞板上的负载时，测试发现输入TLV3501的方波高电平由3.3V被拉低至2.26V左右（即便是在推挽输出的情况下）。查看手册发现，这是由于STM32的IO口最大输出电流仅25mA，带负载能力不足。

![image-20250716092026857](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507162201319.png)

ii. 测试较短的电缆长度（如1m）时，两路下降沿的时间差过短，FPGA常会发生误判——计算相邻周期而非同一周期的两路下降沿间隔。

​	针对上述两个问题，我们改进电路。让STM32输出的激励方波先经过一个比较器以维持电压，再经50Ω以模拟输出电阻为50Ω的信号发生器。输入FPGA的两路信号由 **两路比较器的输出** 改为 **STM32经处理（经过第一路比较器与50Ω）后的激励信号** 与 **阶梯波经第二路比较器后输出的下降沿** 。其中，第一路比较器的阈值电压没有特殊要求，第二路比较器的阈值电压为阶梯波第二级上升阶梯的中点（需通过示波器实测得出）。这样一来，两路信号的时间差被拉大，减小了FPGA误判的可能性。



##### ① 系统框图

​	改进后的系统框图如下。

![image-20250727222952471](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507272230478.png)

##### ② 仿真电路图

​	改进后的仿真电路图如下。

![image-20250728235334617](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507290000089.png)

##### ③ 仿真结果

​	新电路的仿真结果如下。

> 注：与旧电路同样地，仿真结果并不稳定，且仅模拟出了下降部分的阶梯。将下图水平翻转，即为上升部分的情况。

![正确新仿真结果图3](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507301612550.png)

​	水平翻转后，选取较好的一段波形进行分析。如下：

![正确新仿真结果图（分析）](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507311030024.png)

​	洞洞板焊接实物照片：

![IMG_20250727_223351](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507272238640.jpg)

![IMG_20250727_223641](https://raw.githubusercontent.com/undefined-0/image-store/main/PicGo/202507272238641.jpg)

### 2.同轴电缆终端负载测量

……



## 二、STM32F103C8T6核心板接线方法：

* 四线0.96寸OLED屏幕

  * PB7 - I2C1 SDA

  * PB6 - I2C1 SCL

  * 3V3、GND两根电源线


* 外部按键输入

  * PB8 - Length

  * PB9 - Load


* 频率200kHz，占空比50%的方波激励信号输出

  * PA8（TIM1-Channel1）


* FPGA计数结果输入

  * PA0-PA7这8个IO口接FPGA的8个数据输出IO口
    * PA7 是最高有效位（MSB），PA0 是最低有效位（LSB）。

      > PA7 → bit7
      > PA6 → bit6
      > ...
      > PA0 → bit0

* 利用ADC+串联分压原理测电缆终端阻性负载（新方案）
  * GND---待测负载---电缆---B点---50Ω---A点---比较器---STM32输出方波的IO
  * A点（比较器输出） - PB0（v1，ADC1-IN8）
  * B点（50Ω的另一端） - PB1（v2，ADC1-IN9）
  * 待测负载 = 50Ω*v2 / (v1-v2)

* 利用ADC+串联分压原理测电缆终端阻性负载（旧方案）
  * GND---待测负载---电缆---B点---50Ω---A点---STM32输出方波的IO
  * A点（STM32输出方波的IO） - PB0（v1，ADC1-IN8）
  * B点（50Ω的另一端） - PB1（v2，ADC1-IN9）
  * 待测负载 = 50Ω*v2 / (v1-v2)
