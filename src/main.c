/*
 * ESCRAVO - Semáforo de Veículos (Versão com Thread e ISR)
 *
 * Lógica:
 * - O 'main' configura LEDs e a Interrupção no PTA2 (Sinal do Mestre).
 * - A ISR 'master_signal_callback' dispara em QUALQUER borda (subida/descida).
 * - A ISR 'dá' o semáforo 'sem_signal_edge'.
 * - A 'escravo_worker_thread' 'pega' o semáforo e reage.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* 1. Mapeamento de Pinos */
#define LED_GREEN_NODE   DT_ALIAS(led0)
#define LED_RED_NODE     DT_ALIAS(led2)
#define MASTER_SYNC_PORT   DT_NODELABEL(gpioa)
#define MASTER_SIGNAL_PIN  2

/* 2. Dispositivos e LEDs */
static const struct device *master_sync_dev = DEVICE_DT_GET(MASTER_SYNC_PORT);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

/* 3. Semáforo e Callback para a ISR */
K_SEM_DEFINE(sem_signal_edge, 0, K_SEM_MAX_LIMIT); // Semáforo que a ISR vai 'dar'
static struct gpio_callback master_cb_data; // Estrutura de dados da ISR

void leds_off(void)
{
    gpio_pin_set_dt(&led_green, 0);
    gpio_pin_set_dt(&led_red,  0);
}

/* 4. ISR (Callback) do Sinal do Mestre (PTA2) */
void master_signal_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // A ISR faz o mínimo possível: apenas acorda a thread de trabalho
    k_sem_give(&sem_signal_edge);
}

/* 5. Thread de Trabalho do Escravo */
void escravo_worker_thread(void)
{
    printk("Thread de trabalho do Escravo iniciada.\n");
   
    // Lê o estado inicial para evitar uma borda falsa na inicialização
    int last_known_state = gpio_pin_get(master_sync_dev, MASTER_SIGNAL_PIN);
    if (last_known_state == 0) {
        leds_off();
        gpio_pin_set_dt(&led_green, 1);
    } else {
        leds_off();
        gpio_pin_set_dt(&led_red, 1);
    }

    while (1) {
        // Dorme indefinidamente até a ISR (master_signal_callback) acordá-la
        k_sem_take(&sem_signal_edge, K_FOREVER);
       
        // Acordou! Vamos ver qual é o novo estado do pino do Mestre
        int mestre_signal = gpio_pin_get(master_sync_dev, MASTER_SIGNAL_PIN);
       
        // Se o estado não mudou, foi um "glitch" ou interrupção espúria. Ignora.
        if (mestre_signal == last_known_state) {
            continue;
        }

        // Borda de Subida (0 -> 1)
        // Mestre mandou parar (foi de Veículo Verde -> Veículo Amarelo)
        if (mestre_signal == 1) {
            printk("MESTRE: 0->1 (Subida). Acionando AMARELO (1s).\n");
           
            // Aciona Amarelo
            leds_off();
            gpio_pin_set_dt(&led_green, 1);
            gpio_pin_set_dt(&led_red, 1);
           
            // Este é o ÚNICO timer do Escravo.
            k_sleep(K_SECONDS(1));
           
            // Terminou o amarelo, vai para Vermelho
            // (Assumimos que o mestre ainda está HIGH, conforme lógica)
            leds_off();
            gpio_pin_set_dt(&led_red, 1);
            printk("MESTRE: Amarelo completo. Acionando VERMELHO.\n");

            last_known_state = 1;
        }
       
        // Borda de Descida (1 -> 0)
        // Mestre mandou seguir (foi de Pedestre Verde -> Veículo Verde)
        else { // mestre_signal == 0
            printk("MESTRE: 1->0 (Descida). Acionando VERDE.\n");
           
            leds_off();
            gpio_pin_set_dt(&led_green, 1);
           
            last_known_state = 0;
        }
    }
}

/* 6. Função Main (Apenas Configuração) */
int main(void)
{
    int ret;

    /* 6.1. Configura LEDs */
    if (!gpio_is_ready_dt(&led_green) || !gpio_is_ready_dt(&led_red)) {
        printk("Erro: LEDs não estão prontos\n"); return 0;
    }
    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;
    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;


    /* 6.2. Configura PTA2 como entrada (Sinal do Mestre) */
    if (!device_is_ready(master_sync_dev)) {
        printk("Erro: GPIOA não está pronto para PTA2 (Sinal Mestre)\n");
        return;
    }
    ret = gpio_pin_configure(master_sync_dev, MASTER_SIGNAL_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0) {
        printk("Erro ao configurar PTA2 (Sinal Mestre) como input\n");
        return;
    }
   
    /* 6.3. Configura a INTERRUPÇÃO no PTA2 */
    ret = gpio_pin_interrupt_configure(master_sync_dev, MASTER_SIGNAL_PIN, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        printk("Erro ao configurar interrupção no PTA2\n");
        return 0;
    }
   
    // Inicializa a estrutura de callback da ISR
    gpio_init_callback(&master_cb_data, master_signal_callback, BIT(MASTER_SIGNAL_PIN));
    // Adiciona a ISR ao driver do GPIOA
    gpio_add_callback(master_sync_dev, &master_cb_data);
   
    printk("ESCRAVO (Reativo com ISR) Iniciado. Aguardando Mestre.\n");
    return 0;
}

// Define a thread de trabalho do escravo
K_THREAD_DEFINE(escravo_tid, 1024, escravo_worker_thread, NULL, NULL, NULL, 7, 0, 0);