/* USER CODE BEGIN Header */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PUMP_PICK_Pin GPIO_PIN_3
#define PUMP_PICK_GPIO_Port GPIOG
#define VALVE_CLAW_Pin GPIO_PIN_4
#define VALVE_CLAW_GPIO_Port GPIOG
#define GPIO_RESERVED3_Pin GPIO_PIN_5
#define GPIO_RESERVED3_GPIO_Port GPIOG
#define VALVE_LIFT_Pin GPIO_PIN_6
#define VALVE_LIFT_GPIO_Port GPIOG
#define PUMP_LIFT_Pin GPIO_PIN_7
#define PUMP_LIFT_GPIO_Port GPIOG
#define VALVE_PICK_Pin GPIO_PIN_8
#define VALVE_PICK_GPIO_Port GPIOG

/* USER CODE BEGIN Private defines */
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
