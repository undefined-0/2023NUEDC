/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "stdio.h" // 为了在send_length_x_as_binary_and_decimal()函数中调用sprintf()
#include "string.h" // 为了在串口发送时能使用strlen()
#include "math.h" // 为了使用fabsf()函数（求浮点数的绝对值）

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// 用于“对激励方波进行采样，然后只取高电平的部分（电压大于某个门限）做平均”
#define THRESHOLD_1 800 // 设v1高电平判定门限为 800（负载电阻较小，分压少，高电平在0.5V-2V之间，故阈值尽量压低）
#define THRESHOLD_2 300 // 设v2高电平判定门限为 300（负载电阻较小，分压少，高电平在0.5V-2V之间，故阈值尽量压低）
#define SAMPLE_BUFFER_SIZE 1000  // ADC对方波采样的缓冲区大小
// #define IO_DATA_SIZE 5000 // 接收来自FPGA的数据的缓冲区大小

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

// length_x：按下length按键后在IO口读出的数据。（是将PA0~PA7的电平状态合并为一个字节后的结果。是原始计数数据。）
// length_y：对length_x通过换算关系处理后所得的电缆长度。
uint8_t length_x = 0; 
double length_y = 0.0; 

// 按下load按键后在IO口读出的数据。供串口输出用。
uint8_t load_x = 0;

// avg_length_x：length_x（按下length按键后在IO口读出的数据）的平均值。
// avg_load_x：load_x（按下load按键后在IO口读出的数据）的平均值。
double avg_length_x = 0.0;
double avg_load_x = 0.0;

// length_x_buffer[10]：字符串形式的length_x，为了在oled屏幕上显示。
// length_y_buffer[10]：字符串形式的length_y，为了在oled屏幕上显示。
uint8_t length_x_buffer[10];
uint8_t length_y_buffer[10];

// pins[8]：用来存放8个IO电平状态（来自FPGA）的数组。
const uint16_t pins[8] = 
{
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
    GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7
};

// 通过串联分压原理来计算待测负载
// GND---待测负载---B---50Ω---A---比较器---STM32
//                 ^         ^ 
//                 |         | 
//                 v2        v1 
uint16_t adc_values[2]; // 用于DMA接收ADC值，因为使用两个通道故数组大小为2

// 用于“对激励方波进行采样，然后只取高电平的部分（电压大于某个门限）做平均”
uint16_t adc_high_level_samples_1[SAMPLE_BUFFER_SIZE]; // 存储V1方波（A点，经过比较器后的激励信号）的高电平采样点
uint16_t adc_high_level_samples_2[SAMPLE_BUFFER_SIZE]; // 存储V2方波（B点，分压后）的高电平采样点
uint16_t high_sample_count_1 = 0; // V1高电平采样点计数
uint16_t high_sample_count_2 = 0; // V2高电平采样点计数
float high_level_avg_1 = 0.0f; // V1高电平采样点平均值
float high_level_avg_2 = 0.0f; // V2高电平采样点平均值

// 用于存储串口输出信息的字符串缓冲区数组
uint8_t message[50] = "";

// load：待测阻性负载的值
float load = 0.0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/**
  * @brief  重写DMA传输完成中断回调函数，记录高电平方波值，记录高电平采样点数
  * @param  hadc：句柄指针，例如 &huart1
  * @retval 无
  */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    // 处理 Channel 8（adc_values[0]）上的方波（A点，激励信号，V1）
    if (adc_values[0] > THRESHOLD_1)
    {
        if (high_sample_count_1 < SAMPLE_BUFFER_SIZE)
        {
            adc_high_level_samples_1[high_sample_count_1++] = adc_values[0];
        }
    }

        // 处理 Channel 9（adc_values[1]）上的方波（B点，分压后，V2）
    if (adc_values[1] > THRESHOLD_2)
    {
        if (high_sample_count_2 < SAMPLE_BUFFER_SIZE)
        {
            adc_high_level_samples_2[high_sample_count_2++] = adc_values[1];
        }
    }
}


/**
  * @brief  读取 PA0~PA7 的电平状态，合并为一个字节返回。
  * PA7 是最高有效位（MSB）,PA0 是最低有效位（LSB）。
  * 
  * PA7 → bit7
  * PA6 → bit6
  * ...
  * PA0 → bit0
  * 
  * 在 while 循环中调用length_x = read_8_io();
  * 则length_x即为来自FPGA的计数结果。
  * @retval uint8_t
  */
uint8_t read_8_io(void)
{
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        if (HAL_GPIO_ReadPin(GPIOA, pins[i]) == GPIO_PIN_SET) {
            value |= (1 << i);  // 对应位设为1
        }
    }
    return value;
}


/**
  * @brief  将给定的字节数据以二进制和十进制字符串形式通过串口发送出去
  *         该函数用于将 uint8_t 类型的 length_x 转换为直观的二进制表示和十进制数值，
  *         并通过指定的 UART 发送。
  * @param  huart：UART句柄指针，例如 &huart1
  * @param  u8data：要发送的 uint8_t 类型数据（0~255）
  * @retval 无
  */
void send_length_x_as_binary_and_decimal(UART_HandleTypeDef *huart, uint8_t u8data)
{
    uint8_t tx_buffer[30]; // 发送缓冲区大小
    int index = 0;

    // 添加二进制表示
    index += sprintf(tx_buffer + index, "Bin(2): ");
    for (int i = 7; i >= 0; i--) {
        if (u8data & (1 << i)) {
            tx_buffer[index++] = '1';
        } else {
            tx_buffer[index++] = '0';
        }
    }

    // 添加分隔符
    tx_buffer[index++] = '\r'; // 回车
    tx_buffer[index++] = '\n'; // 换行

    // 添加十进制表示
    index += sprintf(tx_buffer + index, "Dec(10): %d\r\n", u8data);

    // 每段数据发送完后空一行
    tx_buffer[index++] = '\n';

    // 发送数据
    HAL_UART_Transmit(huart, (uint8_t*)tx_buffer, index, HAL_MAX_DELAY);
}


/**
  * @brief  计算输入ADC的方波的高电平部分的平均值并写入用于串口发送的字符串
  *         如果采样到了有效的高电平（即：有超过阈值的采样点），则计算其平均值；
  *         否则输出“无高电平样本”的提示信息。
  * @param  adc_high_level_samples：指向ADC高电平样本数组的指针
  * @param  high_sample_count：高电平样本的数量
  * @param  message：用于存储输出信息的字符串缓冲区
  * @param  v_id：电压数组标识符，1 表示 adc_high_level_samples_1，
  *                              2 表示 adc_high_level_samples_2
  * @retval float类型的电压平均值
  */
float calculate_and_display_high_level_avg(uint16_t *adc_high_level_samples, uint16_t high_sample_count, uint8_t *message, int v_id)
{
    if (high_sample_count > 0)
    {
        float high_level_avg = 0.0f;
        for (uint16_t i = 0; i < high_sample_count; i++)
        {
            high_level_avg += adc_high_level_samples[i];
        }
        high_level_avg /= high_sample_count;

        // 根据 v_id 决定前缀名称
        const uint8_t *prefix = (v_id == 1) ? "V1 High Avg" : "V2 High Avg";
        
        // 格式化输出平均值和样本数量
        sprintf(message, "%s: %.2f (%lu samples)\n", prefix, high_level_avg, high_sample_count);
        return high_level_avg;
    }
    else
    {
        strcpy(message, "No high level samples detected.\n");
		return 0;
    }
}

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_1); // 开启PWM输出（1MHz，占空比50%）
  HAL_ADCEx_Calibration_Start(&hadc1); // 校准ADC

  HAL_ADC_Start_DMA(&hadc1,(uint32_t*)adc_values,sizeof(adc_values)/sizeof(uint16_t));
  // 触发ADC采样与转换，且用DMA搬运。搬运到的内存地址是adc_values数组名，
  // 搬运次数为sizeof(adc_values)/sizeof(uint16_t)=2次（因为是两个通道。也可以直接填2）。
  // 因配置了连续转换模式，只需要触发一次，故写在while(1)循环外。

  HAL_ADC_PollForConversion(&hadc1,HAL_MAX_DELAY); // 轮询检查转换是否完成，转换完成后再读取测量结果，以免读到默认值。

  OLED_Init();                           // OLED初始化
  OLED_Clear();                          // 清屏
  OLED_ShowString(0,0,"Waiting for detection",12, 0);    // 正相显示8X16字符串

 
//  OLED_ShowCHinese(0,4,0,1); // 反相显示汉字“独”
//  OLED_ShowCHinese(16,4,1,1);// 反相显示汉字“角”
//  OLED_ShowCHinese(32,4,2,1);// 反相显示汉字“兽”
//  OLED_ShowCHinese(0,6,0,0); // 正相显示汉字“独”
//  OLED_ShowCHinese(16,6,1,0);// 正相显示汉字“角”
//  OLED_ShowCHinese(32,6,2,0);// 正相显示汉字“兽”

//  OLED_ShowNum(48,4,6,1,16, 0);// 正相显示1位8X16数字“6”
//  OLED_ShowNum(48,7,77,2,12, 1);// 反相显示2位6X8数字“77”
//  OLED_DrawBMP(90,0,122, 4,BMP1,0);// 正相显示图片BMP1
//  OLED_DrawBMP(90,4,122, 8,BMP1,1);// 反相显示图片BMP1
 
//  OLED_HorizontalShift(0x26);// 全屏水平向右滚动播放

// 显示正负浮点数的代码
//  float num1=-231.24;
//  float num2=23.375;

// OLED_ShowString(0,0,"Show Decimal",12,0);
// OLED_Showdecimal(0,4,num1,3,2,12, 0);
// OLED_Showdecimal(0,6,num2,2,3,16, 1);

 
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // 可能因为没延时所以加上这两句之后按下按键就会跑飞，暂不用
    // length_x = read_8_io();
    // send_length_x_as_binary_and_decimal(&huart1, length_x); // 调试中通过串口查看PA0~PA7每一位的具体情况

    /*-------------------------正式程序-------------------------*/

    /* 检查Length键是否被按下（ PA8 是否被拉低 ）*/
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET)
    {
        HAL_Delay(55); // 硬件消抖在电路扩展后失效了，采用软件消抖
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET)
        {
            HAL_UART_Transmit(&huart1, (uint8_t*)"Length\r\n", 7, HAL_MAX_DELAY);

            length_x = read_8_io();
            send_length_x_as_binary_and_decimal(&huart1, length_x); // 调试中通过串口查看PA0~PA7每一位的具体情况

            const uint16_t sample_count = 5000;
            double sum_length_x = 0.0;
            for (uint16_t i = 0; i < sample_count; i++)
            {
                sum_length_x += read_8_io();  // 累加每次的读数
            }
            avg_length_x = sum_length_x / sample_count;  // 求平均
            length_y = avg_length_x*0.1033-1.1627; // 计算得电缆长度值
            uint8_t length_y_buffer[32];
            sprintf(length_y_buffer, "Length: %.4fm", length_y); 
            OLED_ShowString(0, 2, (uint8_t*)length_y_buffer, 16, 0);
        }
    }

    /* 检查Load键是否被按下（ PA9 是否被拉低 ）*/
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET)
    {
        HAL_Delay(55); // 硬件消抖在电路扩展后失效了，采用软件消抖
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET)
        {
            HAL_UART_Transmit(&huart1, (uint8_t*)"Load\r\n", 5, HAL_MAX_DELAY);

            const uint16_t sample_count = 5000;
            double sum_load_x = 0.0;
            for (uint16_t i = 0; i < sample_count; i++)
            {
                sum_load_x += read_8_io();  // 累加每次的读数
            }
            avg_load_x = sum_load_x / sample_count;  // 求平均

            // 显示 Load 平均值
            uint8_t load_y_buffer[32];

            // 比较 avg_length_x 和 avg_load_x 的差值
            double diff = fabs(avg_length_x - avg_load_x);

            high_level_avg_1 = calculate_and_display_high_level_avg(adc_high_level_samples_1, high_sample_count_1, message, 1); // V1高电平采样点平均值
                HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), HAL_MAX_DELAY);
                high_level_avg_2 = calculate_and_display_high_level_avg(adc_high_level_samples_2, high_sample_count_2, message, 2); // V2高电平采样点平均值
                HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), HAL_MAX_DELAY);

            if (high_level_avg_1 < 2500) // 若按下load按键时v1有明显的拉低（空载/接入电容时实测应为4000+），则判断负载为电阻
            {
                // 进入电阻值计算逻辑
                // 计算输入ADC的V1、V2方波的高电平部分的平均值并通过串口输出
                length_x = read_8_io();
                send_length_x_as_binary_and_decimal(&huart1, length_x); // 调试中通过串口查看PA0~PA7每一位的具体情况

                high_level_avg_1 = calculate_and_display_high_level_avg(adc_high_level_samples_1, high_sample_count_1, message, 1); // V1高电平采样点平均值
                HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), HAL_MAX_DELAY);
                high_level_avg_2 = calculate_and_display_high_level_avg(adc_high_level_samples_2, high_sample_count_2, message, 2); // V2高电平采样点平均值
                HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), HAL_MAX_DELAY);

                // 通过串联分压原理计算负载电阻
                if(fabsf(high_level_avg_1 - high_level_avg_2) < 20.0f)
                  load = 999; // 开路，负载无穷大
                else
                  load = high_level_avg_2*50.0/(high_level_avg_1-high_level_avg_2);
                  load = load*0.9005-0.5152;
                  // load = load*1.1105-1.4221;
                  
                  // load = load*load*0.015+load*0.2777+4.8421;

                sprintf(message,"V1: %.2f V2: %.2f load: %.2f\n",high_level_avg_1,high_level_avg_2,load); // 将PB0、PB1采到的数据及算出的负载值写入数组message，用于串口发送
                // sprintf(message,"load: %.2f\n",load); // 将算出的负载值写入数组message，用于串口发送
                HAL_UART_Transmit(&huart1,(uint8_t*)message,strlen(message),HAL_MAX_DELAY);
                
                // 清空缓冲区供下一轮使用
                high_sample_count_1 = 0;
                high_sample_count_2 = 0;
                sprintf(load_y_buffer, "Load:R: %.4f",load);
                OLED_ShowString(0, 6, (uint8_t*)load_y_buffer, 16, 0); // 显示电阻值于OLED屏幕第6行
            }
            else // 若按下length按键和load按键时读取到的IO口数据不同（时间差不同），则判断负载为电容
            {
                /*-----------调试用-----------*/
                load_x = read_8_io();
                send_length_x_as_binary_and_decimal(&huart1, load_x); // 调试中通过串口查看PA0~PA7每一位的具体情况
                /*-----------调试用-----------*/
                length_x = read_8_io();
                // 进入电容值计算逻辑
                double cap_diff = diff; // 差值用于电容计算
                // uint8_t diff_buffer[32];
                // cap_diff = cap_diff*9.8214+25.714;
                //cap_diff = cap_diff*9.8214+25.714;
                                if(cap_diff<10)
                {
                cap_diff = -0.1673 * cap_diff * cap_diff + 10.487 * cap_diff - 22.27+37;
                }
                else{cap_diff = -0.1673 * cap_diff * cap_diff + 10.487 * cap_diff - 22.27+77;}
                
                sprintf(load_y_buffer, "Load:C: %.2f", cap_diff);
                OLED_ShowString(0, 6, (uint8_t*)load_y_buffer, 16, 0); // 显示差值于OLED屏幕第6行
                    high_sample_count_1 = 0;
    high_sample_count_2 = 0;
            }
        }
    }

     /*-------------------------正式程序-------------------------*/


     /*-------------------------测电阻（直流检测版）-------------------------*/
    // HAL_Delay(100);
    // if(adc_values[0]==adc_values[1])
    //   load = 999; // 开路，负载无穷大
    // else
    //   load = adc_values[1]*50/(adc_values[0]-adc_values[1]);
    // sprintf(message,"%d %d %.2f",adc_values[0],adc_values[1],load); // 将PB0、PB1采到的数据及算出的负载值写入数组message，用于串口发送
    // HAL_UART_Transmit(&huart1,(uint8_t*)message,strlen(message),HAL_MAX_DELAY);
    /*-------------------------测电阻（直流检测版）-------------------------*/


    /*-------------------------测电阻（方波平均版）-------------------------*/
    // HAL_Delay(500);  // 每 500ms 计算一次高电平平均值

    // // 计算输入ADC的V1、V2方波的高电平部分的平均值并通过串口输出
    // high_level_avg_1 = calculate_and_display_high_level_avg(adc_high_level_samples_1, high_sample_count_1, message, 1); // V1高电平采样点平均值
    // //HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), HAL_MAX_DELAY);
    // high_level_avg_2 = calculate_and_display_high_level_avg(adc_high_level_samples_2, high_sample_count_2, message, 2); // V2高电平采样点平均值
    // //HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), HAL_MAX_DELAY);

    // // 通过串联分压原理计算负载电阻
    // if(fabsf(high_level_avg_1 - high_level_avg_2) < 20.0f)
    //   load = 999; // 开路，负载无穷大
    // else
    //   load = high_level_avg_2*50.0/(high_level_avg_1-high_level_avg_2);
    // sprintf(message,"V1: %.2f V2: %.2f load: %.2f\n",high_level_avg_1,high_level_avg_2,load); // 将PB0、PB1采到的数据及算出的负载值写入数组message，用于串口发送
    // // sprintf(message,"load: %.2f\n",load); // 将算出的负载值写入数组message，用于串口发送
    // HAL_UART_Transmit(&huart1,(uint8_t*)message,strlen(message),HAL_MAX_DELAY);
    // // 清空缓冲区供下一轮使用
    // high_sample_count_1 = 0;
    // high_sample_count_2 = 0;
    /*-------------------------测电阻（方波平均版）-------------------------*/
    


    /*-------------------------测IO读取-------------------------*/
    // length_x = read_8_io();
    // //HAL_UART_Transmit(&huart1, &length_x, 1, HAL_MAX_DELAY); // 原生串口发送，只能正常查看16进制数，不直观，不采用
    // send_length_x_as_binary_and_decimal(&huart1, length_x);
    // sprintf((char*)length_x_buffer, "Length: %d", length_x); // 将length_x转换为字符串形式，存入数组length_x_buffer[]，为在屏幕上显示做准备
    // OLED_ShowString(0, 4, length_x_buffer, 16, 0);
    /*-------------------------测IO读取-------------------------*/



    /*-------------------------测按键、屏幕-------------------------*/
    // /* 检查Length键是否被按下（ PA8 是否被拉低 ）*/
    // if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET)
    // {
    //     HAL_UART_Transmit(&huart1, (uint8_t*)"Length\r\n", 7, HAL_MAX_DELAY);
		//     OLED_ShowString(0,4,"Length:",16, 0);    // 正相显示8X16字符串
    //     length_x = read_8_io();
    //     // HAL_UART_Transmit(&huart1, &length_x, 1, HAL_MAX_DELAY); // 会输出16进制值，无法正常读取，不使用
    // }

    // /* 检查Load键是否被按下（ PA9 是否被拉低 ）*/
    // if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET)
    // {
    //     HAL_UART_Transmit(&huart1, (uint8_t*)"Load\r\n", 5, HAL_MAX_DELAY);
		//     OLED_ShowString(0,6,"Load:",16,0);// 正相显示6X8字符串
    // }
    /*-------------------------测按键、屏幕-------------------------*/


    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
