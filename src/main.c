#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/* Referência aos LEDs no Device Tree */
#define LED_RED_NODE     DT_ALIAS(led0)
#define LED_YELLOW_NODE  DT_ALIAS(led1)
#define LED_GREEN_NODE   DT_ALIAS(led2)

#if !DT_NODE_HAS_STATUS(LED_RED_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED_YELLOW_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED_GREEN_NODE, okay)
#error "Faltam alias led0, led1 ou led2 no Device Tree"
#endif

static const struct gpio_dt_spec led_red    = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_yellow = GPIO_DT_SPEC_GET(LED_YELLOW_NODE, gpios);
static const struct gpio_dt_spec led_green  = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);

/* Mutex para garantir exclusão mútua (apenas um LED ligado por vez) */
K_MUTEX_DEFINE(led_mutex);

/* Função auxiliar para ligar um LED e desligar os outros */
void set_led_state(const struct gpio_dt_spec *led_on)
{
    gpio_pin_set_dt(&led_red,    led_on == &led_red);
    gpio_pin_set_dt(&led_yellow, led_on == &led_yellow);
    gpio_pin_set_dt(&led_green,  led_on == &led_green);
}

void red_thread(void)
{
    while (1) {
        k_mutex_lock(&led_mutex, K_FOREVER);
        set_led_state(&led_red);
        k_msleep(4000);
        k_mutex_unlock(&led_mutex);
        k_msleep(1); // pequena pausa para liberar CPU
    }
}

void yellow_thread(void)
{
    while (1) {
        k_mutex_lock(&led_mutex, K_FOREVER);
        set_led_state(&led_yellow);
        k_msleep(1000);
        k_mutex_unlock(&led_mutex);
        k_msleep(1);
    }
}

void green_thread(void)
{
    while (1) {
        k_mutex_lock(&led_mutex, K_FOREVER);
        set_led_state(&led_green);
        k_msleep(3000);
        k_mutex_unlock(&led_mutex);
        k_msleep(1);
    }
}

/* Criação das 3 threads */
K_THREAD_DEFINE(red_tid,    512, red_thread,    NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(yellow_tid, 512, yellow_thread, NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(green_tid,  512, green_thread,  NULL, NULL, NULL, 1, 0, 0);

void main(void)
{
    int ret;

    const struct gpio_dt_spec *leds[] = { &led_red, &led_yellow, &led_green };

    /* Configura pinos como saída */
    for (int i = 0; i < 3; i++) {
        if (!gpio_is_ready_dt(leds[i])) {
            printk("Erro: LED %d não está pronto\n", i);
            return;
        }
        ret = gpio_pin_configure_dt(leds[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            printk("Erro %d ao configurar LED %d\n", ret, i);
            return;
        }
    }

    printk("Sistema de semáforo iniciado!\n");
}
