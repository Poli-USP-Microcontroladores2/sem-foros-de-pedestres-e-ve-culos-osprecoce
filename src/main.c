/*
 * Semáforo de Pedestres com Mutex e Modo Noturno
 * Placa: FRDM-KL25Z
 *
 * Mapeamento de LEDs (Conforme solicitado):
 * - led0: LED Verde
 * - led1: LED Vermelho
 *
 * Modo Normal:
 * - Verde: 4s ligado
 * - Vermelho: 2s ligado
 *
 * Modo Noturno:
 * - Verde: Desligado
 * - Vermelho: Pisca (1s ligado, 1s desligado)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestre, LOG_LEVEL_DBG);

/* 1. Definição dos LEDs (CORRIGIDO) */
// Conforme sua especificação: Verde = 0, Vermelho = 1
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/* 2. Definição do Mutex */
K_MUTEX_DEFINE(led_mutex);

/* 3. NOVO: Flag de Controle do Modo Noturno */
// Mude para 'false' para testar o modo normal.
static volatile bool g_night_mode = false;

/* 4. Definição das Stacks para as Threads */
#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(green_thread_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(red_thread_stack, THREAD_STACK_SIZE);

static struct k_thread green_thread_data;
static struct k_thread red_thread_data;

/*
 * 5. Lógica da Thread do LED Verde (led0)
 */
void green_thread_function(void *a, void *b, void *c)
{
    while (1) {
        k_mutex_lock(&led_mutex, K_FOREVER);

        // SÓ executa a lógica se NÃO estiver no modo noturno
        if (!g_night_mode) {
            LOG_INF("LED Verde (led0): ON");
            gpio_pin_set_dt(&led_green, 1);
            k_sleep(K_SECONDS(4));
            gpio_pin_set_dt(&led_green, 0);
            LOG_INF("LED Verde (led0): OFF");
        }
        
        k_mutex_unlock(&led_mutex);

        if (g_night_mode) {
            k_sleep(K_SECONDS(1));
        } else {
            k_sleep(K_MSEC(50));
        }
    }
}

/*
 * 6. Lógica da Thread do LED Vermelho (led1)
 */
void red_thread_function(void *a, void *b, void *c)
{
    while (1) {
        k_mutex_lock(&led_mutex, K_FOREVER);

        if (g_night_mode) {
            // LÓGICA MODO NOTURNO
            LOG_INF("Modo Noturno: Vermelho (led1) ON");
            gpio_pin_set_dt(&led_red, 1);
            k_sleep(K_SECONDS(1)); 

            LOG_INF("Modo Noturno: Vermelho (led1) OFF");
            gpio_pin_set_dt(&led_red, 0);

        } else {
            // LÓGICA MODO NORMAL
            LOG_INF("LED Vermelho (led1): ON");
            gpio_pin_set_dt(&led_red, 1);
            k_sleep(K_SECONDS(2)); 
            
            gpio_pin_set_dt(&led_red, 0);
            LOG_INF("LED Vermelho (led1): OFF");
        }
        
        k_mutex_unlock(&led_mutex);

        if (g_night_mode) {
            k_sleep(K_SECONDS(1));
        } else {
            k_sleep(K_MSEC(50));
        }
    }
}

/*
 * 7. Função Main
 *
 * Configura os LEDs e inicia as duas threads.
 */
int main(void)
{
    int ret;

    // A lógica aqui não muda, pois usamos as variáveis led_red e led_green
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("Dispositivo de LED não está pronto.");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar LED vermelho (led1).");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar LED verde (led0).");
        return 0;
    }

    gpio_pin_set_dt(&led_red, 0);
    gpio_pin_set_dt(&led_green, 0);

    if (g_night_mode) {
        LOG_INF("Semáforo Iniciado em MODO NOTURNO");
    } else {
        LOG_INF("Semáforo Iniciado em MODO NORMAL");
    }

    k_thread_create(&green_thread_data, green_thread_stack,
                    K_THREAD_STACK_SIZEOF(green_thread_stack),
                    green_thread_function,
                    NULL, NULL, NULL,
                    THREAD_PRIORITY, 0, K_NO_WAIT);

    k_thread_create(&red_thread_data, red_thread_stack,
                    K_THREAD_STACK_SIZEOF(red_thread_stack),
                    red_thread_function,
                    NULL, NULL, NULL,
                    THREAD_PRIORITY, 0, K_NO_WAIT);

    return 0;
}