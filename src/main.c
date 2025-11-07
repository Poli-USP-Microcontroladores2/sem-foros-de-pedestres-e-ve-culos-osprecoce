/*
 * ESCRAVO - Semáforo de Veículos (Versão Reativa Sincronizada)
 *
 * Lógica:
 * - Sem threads, sem timers de ciclo.
 * - Reage 100% ao sinal do MESTRE no PTA2.
 * - PTA2 (LOW) = Mostra VERDE.
 * - PTA2 (HIGH) = Mostra VERMELHO.
 * - PTA2 (Borda 0->1) = Mostra AMARELO por 1s, DEPOIS Vermelho.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* Mapeamento FRDM-KL25Z */
#define LED_GREEN_NODE   DT_ALIAS(led0)
#define LED_RED_NODE     DT_ALIAS(led2)

/* Definição da porta PTA2 como entrada (Sinal do Mestre) */
#define MASTER_SYNC_PORT   DT_NODELABEL(gpioa)
#define MASTER_SIGNAL_PIN  2

static const struct device *master_sync_dev = DEVICE_DT_GET(MASTER_SYNC_PORT);

#if !DT_NODE_HAS_STATUS(LED_GREEN_NODE, okay)
#error "Falta alias led0 no Device Tree"
#endif
#if !DT_NODE_HAS_STATUS(LED_RED_NODE, okay)
#error "Falta alias led2 no Device Tree"
#endif

static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

/* Estados internos do Escravo */
enum state {
    VEHICLE_GREEN,
    VEHICLE_YELLOW,
    VEHICLE_RED
};

void leds_off(void)
{
    gpio_pin_set_dt(&led_green, 0);
    gpio_pin_set_dt(&led_red,  0);
}

void main(void)
{
    int ret;
    enum state current_state = VEHICLE_RED; // Inicia vermelho por segurança
    int last_master_signal = 1; // Assume que mestre começa em HIGH

    /* 1. Configura LEDs */
    if (!gpio_is_ready_dt(&led_green)) {
        printk("Erro: LED Verde não está pronto\n"); return;
    }
    if (!gpio_is_ready_dt(&led_red)) {
        printk("Erro: LED Vermelho não está pronto\n"); return;
    }
    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return;
    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return;


    /* 2. Configura PTA2 como entrada (Sinal do Mestre) */
    if (!device_is_ready(master_sync_dev)) {
        printk("Erro: GPIOA não está pronto para PTA2 (Sinal Mestre)\n");
        return;
    }
    ret = gpio_pin_configure(master_sync_dev, MASTER_SIGNAL_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0) {
        printk("Erro ao configurar PTA2 (Sinal Mestre) como input\n");
        return;
    }
   
    printk("ESCRAVO (Reativo) Iniciado. Aguardando Mestre.\n");
    leds_off();
    gpio_pin_set_dt(&led_red, 1); // Inicia Vermelho
    last_master_signal = gpio_pin_get(master_sync_dev, MASTER_SIGNAL_PIN);

    /* 3. Loop Principal Reativo (Sem threads) */
    while (1) {
       
        int mestre_signal = gpio_pin_get(master_sync_dev, MASTER_SIGNAL_PIN);

        // Detecta Borda de Subida (0 -> 1)
        // Mestre mandou parar (foi de Veículo Verde -> Veículo Amarelo)
        if (last_master_signal == 0 && mestre_signal == 1) {
            printk("MESTRE: 0->1. Acionando AMARELO (1s).\n");
           
            // Aciona Amarelo
            current_state = VEHICLE_YELLOW;
            leds_off();
            gpio_pin_set_dt(&led_green, 1);
            gpio_pin_set_dt(&led_red, 1);
           
            // Este é o ÚNICO timer do Escravo.
            // É o tempo de amarelo, acionado pelo Mestre.
            k_sleep(K_SECONDS(1));
           
            // Terminou o amarelo, vai para Vermelho
            current_state = VEHICLE_RED;
            leds_off();
            gpio_pin_set_dt(&led_red, 1);
            printk("MESTRE: Amarelo completo. Acionando VERMELHO.\n");
        }
       
        // Detecta Borda de Descida (1 -> 0)
        // Mestre mandou seguir (foi de Pedestre Verde -> Veículo Verde)
        else if (last_master_signal == 1 && mestre_signal == 0) {
            printk("MESTRE: 1->0. Acionando VERDE.\n");
           
            current_state = VEHICLE_GREEN;
            leds_off();
            gpio_pin_set_dt(&led_green, 1);
        }

        // Se o estado for mantido (0->0 ou 1->1), não faz nada.
        // O Escravo apenas mantém seu último estado (Verde ou Vermelho).

        last_master_signal = mestre_signal;
        k_msleep(50); // Polling do pino do Mestre
    }
}
