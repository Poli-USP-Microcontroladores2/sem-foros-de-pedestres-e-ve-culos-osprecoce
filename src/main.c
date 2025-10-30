#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/* Mapeamento correto da FRDM-KL25Z:
   led0 = Verde
   led1 = Azul (não usar)
   led2 = Vermelho
*/
#define LED_GREEN_NODE   DT_ALIAS(led0)
#define LED_RED_NODE     DT_ALIAS(led2)

#if !DT_NODE_HAS_STATUS(LED_GREEN_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED_RED_NODE, okay)
#error "Faltam alias led0 ou led2 no Device Tree"
#endif

static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

/* Semáforos para sincronizar a ordem das threads */
K_SEM_DEFINE(sem_green, 1, 1);   // Começa com 1 para iniciar pelo verde
K_SEM_DEFINE(sem_yellow, 0, 1);
K_SEM_DEFINE(sem_red, 0, 1);

/* Mutex para garantir acesso exclusivo aos LEDs */
K_MUTEX_DEFINE(led_mutex);

/* Desliga leds */
void leds_off(void)
{
    gpio_pin_set_dt(&led_green, 0);
    gpio_pin_set_dt(&led_red, 0);
}

/* GREEN THREAD (3s) */
void green_thread(void)
{
    while (1) {
        k_sem_take(&sem_green, K_FOREVER);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_green, 1);   // Acende verde
        k_msleep(3000);
        k_mutex_unlock(&led_mutex);

        k_sem_give(&sem_yellow); // Libera amarelo
    }
}

/* YELLOW THREAD (1s) - Verde + Vermelho */
void yellow_thread(void)
{
    while (1) {
        k_sem_take(&sem_yellow, K_FOREVER);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 1);     // Amarelo = Verde + Vermelho
        k_msleep(1000);
        k_mutex_unlock(&led_mutex);

        k_sem_give(&sem_red); // Libera vermelho
    }
}

/* RED THREAD (4s) */
void red_thread(void)
{
    while (1) {
        k_sem_take(&sem_red, K_FOREVER);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_red, 1);     // Acende vermelho
        k_msleep(4000);
        k_mutex_unlock(&led_mutex);

        k_sem_give(&sem_green); // Volta para o verde
    }
}

/* Threads */
K_THREAD_DEFINE(green_tid,  512, green_thread,  NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(yellow_tid, 512, yellow_thread, NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(red_tid,    512, red_thread,    NULL, NULL, NULL, 1, 0, 0);

void main(void)
{
    int ret;
    const struct gpio_dt_spec *leds[] = { &led_green, &led_red };

    for (int i = 0; i < 2; i++) {
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

    printk("Semáforo iniciado com 3 threads e semáforos!\n");
}
