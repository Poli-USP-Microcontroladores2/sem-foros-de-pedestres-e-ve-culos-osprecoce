/*
 * Semáforo de Pedestres com Mutex no Zephyr
 * Placa: FRDM-KL25Z
 *
 * LEDs na FRDM-KL25Z (pelos aliases padrão do Zephyr):
 * - led0: LED Vermelho (PTB18)
 * - led1: LED Verde (PTB19)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestre, LOG_LEVEL_DBG);

/* 1. Definição dos LEDs usando Device Tree Aliases */
// O alias 'led0' é o LED Vermelho na FRDM-KL25Z
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
// O alias 'led1' é o LED Verde na FRDM-KL25Z
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* 2. Definição do Mutex */
// Este mutex vai garantir que apenas uma thread (vermelha ou verde)
// possa acessar os LEDs de cada vez.
K_MUTEX_DEFINE(led_mutex);

/* 3. Definição das Stacks para as Threads */
// Cada thread precisa de sua própria área de stack.
#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(green_thread_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(red_thread_stack, THREAD_STACK_SIZE);

static struct k_thread green_thread_data;
static struct k_thread red_thread_data;

/*
 * 4. Lógica da Thread do LED Verde
 *
 * Esta thread controla o LED verde.
 * Ela tenta "travar" o mutex. Quando consegue:
 * 1. Acende o LED verde.
 * 2. Espera 4 segundos.
 * 3. Apaga o LED verde.
 * 4. Libera o mutex para a outra thread.
 */
void green_thread_function(void *a, void *b, void *c)
{
    while (1) {
        // Tenta pegar o mutex. Se estiver em uso (pela thread vermelha),
        // esta thread irá dormir até que ele seja liberado.
        k_mutex_lock(&led_mutex, K_FOREVER);

        LOG_INF("LED Verde: ON");
        gpio_pin_set_dt(&led_green, 1);
        k_sleep(K_SECONDS(4));
        gpio_pin_set_dt(&led_green, 0);
        LOG_INF("LED Verde: OFF");

        // Libera o mutex para a thread vermelha poder execut_ar
        k_mutex_unlock(&led_mutex);

        // Um pequeno sleep fora do mutex para garantir que a outra
        // thread tenha a chance de rodar e pegar o mutex.
        k_sleep(K_MSEC(50));
    }
}

/*
 * 5. Lógica da Thread do LED Vermelho
 *
 * Esta thread controla o LED vermelho.
 * O comportamento é idêntico ao da verde, mas com tempo diferente.
 */
void red_thread_function(void *a, void *b, void *c)
{
    while (1) {
        // Tenta pegar o mutex.
        k_mutex_lock(&led_mutex, K_FOREVER);

        LOG_INF("LED Vermelho: ON");
        gpio_pin_set_dt(&led_red, 1);
        k_sleep(K_SECONDS(2));
        gpio_pin_set_dt(&led_red, 0);
        LOG_INF("LED Vermelho: OFF");
        
        // Libera o mutex
        k_mutex_unlock(&led_mutex);

        k_sleep(K_MSEC(50));
    }
}

/*
 * 6. Função Main
 *
 * Ponto de entrada principal.
 * Configura os LEDs e inicia as duas threads.
 */
int main(void)
{
    int ret;

    // --- Configuração dos LEDs ---
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("Dispositivo de LED não está pronto.");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar LED vermelho.");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar LED verde.");
        return 0;
    }

    // Garantir que ambos os LEDs comecem desligados
    gpio_pin_set_dt(&led_red, 0);
    gpio_pin_set_dt(&led_green, 0);

    LOG_INF("Semáforo de Pedestres Iniciado (FRDM-KL25Z)");

    // --- Inicialização das Threads ---
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