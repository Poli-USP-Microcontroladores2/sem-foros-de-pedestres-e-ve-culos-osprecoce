#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

/* Mapeamento FRDM-KL25Z:
   led0 = Verde
   led2 = Vermelho
*/
#define LED_GREEN_NODE   DT_ALIAS(led0)
#define LED_RED_NODE     DT_ALIAS(led2)

/* Definição do Botão PTA1 */
#define BUTTON_PORT  DT_NODELABEL(gpioa)
#define BUTTON_PIN   1

/* NOVO: Definição da porta PTA2 como entrada */
#define INPUT_PORT   DT_NODELABEL(gpioa)
#define INPUT_PIN    2

static const struct device *button_dev = DEVICE_DT_GET(BUTTON_PORT);
/* NOVO: device para PTA2 */
static const struct device *input_dev  = DEVICE_DT_GET(INPUT_PORT);

#if !DT_NODE_HAS_STATUS(LED_GREEN_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED_RED_NODE, okay)
#error "Faltam alias led0 ou led2 no Device Tree"
#endif

static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

/* Semáforos para controlar a sequência */
K_SEM_DEFINE(sem_green, 1, 1);
K_SEM_DEFINE(sem_yellow, 0, 1);
K_SEM_DEFINE(sem_red, 0, 1);

/* Mutex para controlar LEDs */
K_MUTEX_DEFINE(led_mutex);

/* Flag do modo noturno */
static atomic_t night_mode = ATOMIC_INIT(0);   // 0=normal, 1=noturno

/* Flag de pedido de travessia (pedestre) e indicador de pedido em andamento */
static atomic_t ped_request = ATOMIC_INIT(0);  // 0=nenhum, 1=pedido pendente
static atomic_t ped_active  = ATOMIC_INIT(0);  // 0=nenhum, 1=em progresso (RED pedestre)

/* Estado atual do semáforo (para decisões ao receber pedido)
   0 = GREEN, 1 = YELLOW, 2 = RED
*/
static atomic_t current_state = ATOMIC_INIT(0);

/* Funções para controlar flags */
void set_night_mode(bool enable)
{
    atomic_set(&night_mode, enable ? 1 : 0);
    if (enable) {
        printk("Modo noturno ativado.\n");
    } else {
        printk("Modo noturno desativado.\n");
    }
}

bool request_pedestrian_crossing(void)
{
    if (atomic_get(&night_mode)) {
        printk("Pedido de pedestre ignorado: modo noturno ativo.\n");
        return false;
    }

    if (atomic_get(&ped_active)) {
        printk("Pedido de pedestre ignorado: já em progresso.\n");
        return false;
    }

    int state = atomic_get(&current_state);
    if (!(state == 0 || state == 1)) {
        printk("Pedido de pedestre ignorado: estado atual não permite atendimento (estado=%d).\n", state);
        return false;
    }

    atomic_set(&ped_request, 1);
    printk("Pedido de travessia recebido (flag set). Estado atual=%d\n", state);
    return true;
}

void leds_off(void)
{
    gpio_pin_set_dt(&led_green, 0);
    gpio_pin_set_dt(&led_red,  0);
}

static bool sleep_with_checks(uint32_t total_ms)
{
    const uint32_t CHUNK_MS = 100;
    uint32_t elapsed = 0;

    while (elapsed < total_ms) {
        uint32_t t = MIN(CHUNK_MS, total_ms - elapsed);
        k_msleep(t);
        elapsed += t;

        if (atomic_get(&night_mode)) {
            return true;
        }
        if (atomic_get(&ped_request)) {
            return true;
        }
    }
    return false;
}

/* GREEN THREAD */
void green_thread(void)
{
    while (1) {
        if (atomic_get(&night_mode)) {
            k_msleep(100);
            continue;
        }

        k_sem_take(&sem_green, K_FOREVER);
        atomic_set(&current_state, 0);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_green, 1);

        bool interrupted = sleep_with_checks(3000);

        if (interrupted) {
            if (atomic_get(&night_mode)) {
                gpio_pin_set_dt(&led_green, 0);
                k_mutex_unlock(&led_mutex);
                continue;
            }

            if (atomic_get(&ped_request)) {
                gpio_pin_set_dt(&led_green, 0);
                k_mutex_unlock(&led_mutex);
                printk("Green interrompido por pedido: irá para YELLOW (1s) então RED pedestre.\n");
                atomic_set(&current_state, 1);
                k_sem_give(&sem_yellow);
                continue;
            }
        }

        gpio_pin_set_dt(&led_green, 0);
        k_mutex_unlock(&led_mutex);

        k_sem_give(&sem_yellow);
    }
}

/* YELLOW THREAD */
void yellow_thread(void)
{
    while (1) {
        if (atomic_get(&night_mode)) {
            k_msleep(100);
            continue;
        }

        k_sem_take(&sem_yellow, K_FOREVER);
        atomic_set(&current_state, 1);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 1);

        const uint32_t TOTAL_YELLOW_MS = 1000;
        uint32_t elapsed = 0;
        const uint32_t CHUNK_MS = 100;
        bool early_night = false;

        while (elapsed < TOTAL_YELLOW_MS) {
            uint32_t t = MIN(CHUNK_MS, TOTAL_YELLOW_MS - elapsed);
            k_msleep(t);
            elapsed += t;

            if (atomic_get(&night_mode)) {
                early_night = true;
                break;
            }
        }

        if (early_night) {
            leds_off();
            k_mutex_unlock(&led_mutex);
            continue;
        }

        if (atomic_get(&ped_request) && !atomic_get(&ped_active) && !atomic_get(&night_mode)) {
            atomic_set(&ped_request, 0);
            atomic_set(&ped_active, 1);

            leds_off();
            k_mutex_unlock(&led_mutex);

            printk("Yellow terminou: iniciando RED pedestre por 4s.\n");
            k_sem_give(&sem_red);
            continue;
        }

        leds_off();
        k_mutex_unlock(&led_mutex);

        k_sem_give(&sem_red);
    }
}

/* RED THREAD */
void red_thread(void)
{
    while (1) {
        if (atomic_get(&night_mode)) {
            k_msleep(100);
            continue;
        }

        k_sem_take(&sem_red, K_FOREVER);
        atomic_set(&current_state, 2);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_red, 1);

        if (atomic_get(&ped_active)) {
            printk("RED (pedestre) aceso por 4s. Novos pedidos ignorados.\n");
            uint32_t elapsed = 0;
            const uint32_t CHUNK_MS = 100;
            while (elapsed < 4000) {
                k_msleep(CHUNK_MS);
                elapsed += CHUNK_MS;
            }
            atomic_set(&ped_active, 0);
            printk("RED (pedestre) finalizado. Voltando ao ciclo normal (GREEN).\n");
            leds_off();
            k_mutex_unlock(&led_mutex);
            k_sem_give(&sem_green);
            continue;
        } else {
            uint32_t elapsed = 0;
            const uint32_t CHUNK_MS = 100;
            while (elapsed < 4000) {
                k_msleep(CHUNK_MS);
                elapsed += CHUNK_MS;
                if (atomic_get(&night_mode)) break;
            }
            leds_off();
            k_mutex_unlock(&led_mutex);
            k_sem_give(&sem_green);
        }
    }
}

/* NIGHT MODE THREAD – Pisca amarelo */
void night_mode_thread(void)
{
    while (1) {
        if (!atomic_get(&night_mode)) {
            k_msleep(200);
            continue;
        }

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 1);
        k_msleep(1000);
        leds_off();
        k_mutex_unlock(&led_mutex);
        k_msleep(1000);
    }
}

void button_thread(void)
{
    if (!device_is_ready(button_dev)) {
        printk("Erro: GPIOA não está pronto para o botão\n");
        return;
    }

    gpio_pin_configure(button_dev, BUTTON_PIN, GPIO_INPUT | GPIO_PULL_UP);

    uint8_t last_state = 1;

    while (1) {
        uint8_t state = gpio_pin_get(button_dev, BUTTON_PIN);

        if (last_state == 1 && state == 0) {
            printk("Botão de pedestre pressionado\n");
            request_pedestrian_crossing();
        }

        last_state = state;
        k_msleep(50);
    }
}

/* Cria threads */
K_THREAD_DEFINE(green_tid,  512, green_thread,      NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(yellow_tid, 512, yellow_thread,     NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(red_tid,    512, red_thread,        NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(night_tid,  512, night_mode_thread, NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(button_tid, 512, button_thread, NULL, NULL, NULL, 2, 0, 0);

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

    /* NOVO: configuração de PTA2 como entrada */
    if (!device_is_ready(input_dev)) {
        printk("Erro: GPIOA não está pronto para PTA2\n");
        return;
    }

    ret = gpio_pin_configure(input_dev, INPUT_PIN, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0) {
        printk("Erro %d ao configurar PTA2 como input\n", ret);
        return;
    }
    printk("PTA2 configurado como entrada (pull-up habilitado)\n");

    /* Garante que o ciclo inicie por green */
    atomic_set(&current_state, 0);
    k_sem_give(&sem_green);

    /* Exemplo de leitura simples de PTA2 */
    while (1) {
        int val = gpio_pin_get(input_dev, INPUT_PIN);
        printk("Leitura PTA2 = %d\n", val);
        k_msleep(1000);
    }
}
