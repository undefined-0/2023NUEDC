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
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "stdio.h" // 为了在send_length_x_as_binary_and_decimal()函数中调用sprintf()
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

// length_x：将PA0~PA7的电平状态合并为一个字节后的结果
uint8_t length_x = 0; 

// pins[8]：用来存放8个IO电平状态（来自FPGA）的数组
const uint16_t pins[8] = 
{
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
    GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  *         并通过指定的 UART 接口发送。
  * @param  huart：UART句柄指针，例如 &huart1
  * @param  u8data：要发送的 uint8_t 类型数据（0~255）
  * @retval 无
  */
void send_length_x_as_binary_and_decimal(UART_HandleTypeDef *huart, uint8_t u8data)
{
    char tx_buffer[30]; // 缓冲区大小（根据需要调整）
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
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_1);// 开启PWM输出（1MHz，占空比50%）

  OLED_Init();                           // OLED初始化
  OLED_Clear();                          // 清屏
  OLED_ShowString(0,0,"start",16, 0);    // 正相显示8X16字符串

 
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
    HAL_Delay(100);
    length_x = read_8_io();
    //HAL_UART_Transmit(&huart1, &length_x, 1, HAL_MAX_DELAY); // 原生串口发送，只能正常查看16进制数，不直观，不采用
    send_length_x_as_binary_and_decimal(&huart1, length_x);
    // /* 检查 PA3 是否被拉低 */
    // if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET)
    // {
    //     HAL_UART_Transmit(&huart1, (uint8_t*)"Length\r\n", 7, HAL_MAX_DELAY);
		//     OLED_ShowString(0,4,"Length:",16, 0);    // 正相显示8X16字符串
    //     length_x = read_8_io();
    //     HAL_UART_Transmit(&huart1, &length_x, 1, HAL_MAX_DELAY);
    // }

    // /* 检查 PA4 是否被拉低 */
    // if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET)
    // {
    //     HAL_UART_Transmit(&huart1, (uint8_t*)"Load\r\n", 5, HAL_MAX_DELAY);
		//     OLED_ShowString(0,6,"Load:",16,0);// 正相显示6X8字符串
    // }

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
