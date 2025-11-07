/*
 * MESTRE - Semáforo de Pedestres (Versão Reativa Sincronizada)
 *
 * Mapeamento de Pinos:
 * - led0: LED Verde (Pedestre)
 * - led2: LED Vermelho (Pedestre)
 * - PTA1: Botão de Pedestre (Input)
 * - PTA2: Sinal para ESCRAVO (Output)
 *
 * Lógica de Sinal (PTA2):
 * - PTA2 = 0 (LOW): Escravo (Veículo) deve estar VERDE. (3s)
 * - PTA2 = 1 (HIGH): Escravo (Veículo) deve estar AMARELO (1s) ou VERMELHO (4s).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestre_mestre, LOG_LEVEL_DBG);

/* 1. Definição dos LEDs (Usando DT) */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/* 2. Definição Manual (Porta A) */
const struct device *gpioa_dev = DEVICE_DT_GET(DT_NODELABEL(gpioa));
#define BUTTON_PIN 1           // PTA1 (Input)
#define MASTER_SIGNAL_PIN 2    // PTA2 (Output)
static struct gpio_callback button_cb_data;

/* 3. Semáforo para sinalizar o evento do botão */
K_SEM_DEFINE(button_sem, 0, 1); 

/* 4. Flag de Controle do Modo Noturno (Não implementado na lógica reativa) */
// static volatile bool g_night_mode = false;

/* 5. Estados da Máquina */
enum state {
    STATE_VEHICLE_GREEN,   // Veículo pode passar (Pedestre Vermelho)
    STATE_VEHICLE_YELLOW,  // Transição (Pedestre Vermelho)
    STATE_PEDESTRIAN_GREEN // Pedestre pode passar (Veículo Vermelho)
};

/* 6. Função de Callback (ISR) do Botão */
void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // A ISR apenas sinaliza.
    k_sem_give(&button_sem);
}

/* 7. Função Main (Controladora da Máquina de Estados) */
int main(void)
{
    int ret;
    // O Mestre agora controla todos os estados
    enum state current_state = STATE_VEHICLE_GREEN;

    /* 7.1. Configuração dos LEDs (via DT) */
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("Dispositivo de LED não está pronto."); return 0;
    }
    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;
    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;

    /* 7.2. Configuração da Porta A (PTA1 e PTA2) */
    if (!device_is_ready(gpioa_dev)) {
        LOG_ERR("Dispositivo GPIOA (PTA1/PTA2) não está pronto."); return 0;
    }

    // Configura o pino PTA1 (Botão)
    ret = gpio_pin_configure(gpioa_dev, BUTTON_PIN, (GPIO_INPUT | GPIO_PULL_UP));
    if (ret < 0) { LOG_ERR("Falha ao configurar botão PTA1."); return 0; }

    // Configura o pino PTA2 (Sinal Mestre)
    ret = gpio_pin_configure(gpioa_dev, MASTER_SIGNAL_PIN, (GPIO_OUTPUT_INACTIVE)); 
    if (ret < 0) { LOG_ERR("Falha ao configurar PTA2 como saída."); return 0; }

    // Configura a interrupção do botão
    ret = gpio_pin_interrupt_configure(gpioa_dev, BUTTON_PIN, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) { LOG_ERR("Falha ao configurar interrupção do botão."); return 0; }
    
    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(BUTTON_PIN));
    gpio_add_callback(gpioa_dev, &button_cb_data);

    /* 7.3. Define o Estado Inicial */
    LOG_INF("Semáforo MESTRE (Reativo) Iniciado.");
    
    /* 7.4. Loop Principal (Máquina de Estados) */
    while (1) {
        
        switch (current_state) {

        case STATE_VEHICLE_GREEN:
            // <-- TEMPO AJUSTADO AQUI -->
            LOG_INF("Estado: Veículo VERDE (Pedestre Vermelho) por 3s (ou botão)");
            gpio_pin_set_dt(&led_green, 0); // Pedestre VERDE apagado
            gpio_pin_set_dt(&led_red, 1);   // Pedestre VERMELHO aceso
            gpio_pin_set(gpioa_dev, MASTER_SIGNAL_PIN, 0); // Sinal LOW (Verde p/ Veículo)

            k_sem_reset(&button_sem);
            
            // Espera por 3s OU pelo botão
            ret = k_sem_take(&button_sem, K_SECONDS(3)); // <-- TEMPO AJUSTADO AQUI

            if (ret == 0) {
                LOG_INF("Botão pressionado! Interrompendo Veículo Verde.");
            } else {
                LOG_INF("Timeout (3s) do Veículo Verde.");
            }
            
            current_state = STATE_VEHICLE_YELLOW;
            break;

        case STATE_VEHICLE_YELLOW:
            // Tempo de Amarelo (1s)
            LOG_INF("Estado: Veículo AMARELO (Pedestre Vermelho) por 1s");
            gpio_pin_set_dt(&led_green, 0); // Pedestre VERDE apagado
            gpio_pin_set_dt(&led_red, 1);   // Pedestre VERMELHO aceso
            
            // Sinal HIGH (Aviso de parada p/ Veículo)
            gpio_pin_set(gpioa_dev, MASTER_SIGNAL_PIN, 1); 

            k_sleep(K_SECONDS(1)); 
            
            current_state = STATE_PEDESTRIAN_GREEN;
            break;

        case STATE_PEDESTRIAN_GREEN:
            // Tempo do Pedestre Verde (4s)
            LOG_INF("Estado: Pedestre VERDE (Veículo Vermelho) por 4s");
            gpio_pin_set_dt(&led_red, 0);   // Pedestre VERMELHO apagado
            gpio_pin_set_dt(&led_green, 1); // Pedestre VERDE aceso
            
            // Mantém Sinal HIGH (Veículo deve estar Vermelho)
            gpio_pin_set(gpioa_dev, MASTER_SIGNAL_PIN, 1);

            k_sem_reset(&button_sem); // Ignora botões aqui

            k_sleep(K_SECONDS(4));
            
            current_state = STATE_VEHICLE_GREEN; // Volta o ciclo
            break;
        }
        
        k_sleep(K_MSEC(10));
    }
    return 0;
}