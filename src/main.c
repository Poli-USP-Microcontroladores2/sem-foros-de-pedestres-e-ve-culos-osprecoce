/*
 * Semáforo de Pedestres com Ciclo Automático e Botão de Interrupção/Extensão
 * Placa: FRDM-KL25Z
 *
 * Mapeamento de Pinos:
 * - led0: LED Verde
 * - led2: LED "Vermelho" (AZUL na placa, conforme código original)
 * - PTA1: Botão de Pedestre (Configurado manualmente)
 *
 * Modo Normal (Ciclo Automático):
 * - Verde: 4s ligado
 * - Vermelho/Azul: 4s ligado
 * - Repete...
 *
 * Botão (durante o Vermelho/Azul):
 * - 1. Espera 1s (com led Vermelho/Azul ainda aceso)
 * - 2. Interrompe o restante do tempo e vai para o Verde.
 *
 * Botão (durante o Verde):
 * - Prolonga o verde por mais 4s (a partir do clique).
 *
 * Modo Noturno:
 * - Verde: Desligado
 * - Vermelho/Azul: Pisca (1s ligado, 1s desligado)
 */


#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestre, LOG_LEVEL_DBG);

/* 1. Definição dos LEDs (Usando DT) */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
// Usando 'led2' (LED AZUL) conforme o seu código original
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);


/* 2. Definição Manual do Botão (PTA1) - Sem Overlay */
const struct device *gpioa_dev = DEVICE_DT_GET(DT_NODELABEL(gpioa));
#define BUTTON_PIN 1
static struct gpio_callback button_cb_data;


/* 3. Semáforo para sinalizar o evento do botão */
K_SEM_DEFINE(button_sem, 0, 1); // Inicia "vazio" (0), com limite de 1


/* 4. Flag de Controle do Modo Noturno */
static volatile bool g_night_mode = false;


/* 5. Estados da Máquina */
enum state {
    STATE_GREEN,
    STATE_RED,
    STATE_NIGHT_BLINK
};


/*
 * 6. Função de Callback (ISR) do Botão
 */
void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // A ISR apenas sinaliza. A lógica de "o que fazer"
    // depende do estado atual (STATE_GREEN ou STATE_RED).
    if (!g_night_mode) {
        k_sem_give(&button_sem);
    }
}


/*
 * 7. Função Main (Controladora da Máquina de Estados)
 */
int main(void)
{
    int ret;
    enum state current_state;

    /* 7.1. Configuração dos LEDs (via DT) */
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("Dispositivo de LED não está pronto.");
        return 0;
    }
    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;
    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;

    /* 7.2. Configuração do Botão (Manual, sem DT/Overlay) */
    if (!device_is_ready(gpioa_dev)) {
        LOG_ERR("Dispositivo GPIOA (PTA1) não está pronto.");
        return 0;
    }

    // Configura o pino PTA1 como entrada com pull-up interno
    ret = gpio_pin_configure(gpioa_dev, BUTTON_PIN, (GPIO_INPUT | GPIO_PULL_UP));
    if (ret < 0) {
        LOG_ERR("Falha ao configurar botão PTA1.");
        return 0;
    }

    // Configura a interrupção 
    ret = gpio_pin_interrupt_configure(gpioa_dev, BUTTON_PIN, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar interrupção do botão.");
        return 0;
    }
    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(BUTTON_PIN));
    gpio_add_callback(gpioa_dev, &button_cb_data);

    /* 7.3. Define o Estado Inicial */
    if (g_night_mode) {
        LOG_INF("Semáforo Iniciado em MODO NOTURNO");
        current_state = STATE_NIGHT_BLINK;
        gpio_pin_set_dt(&led_green, 0); 
    } else {
        LOG_INF("Semáforo Iniciado em MODO NORMAL (Ciclo Automático)");
        current_state = STATE_GREEN; // Inicia no Verde
        gpio_pin_set_dt(&led_red, 0); // Garante que o azul comece apagado
    }

    /* 7.4. Loop Principal (Máquina de Estados) */
    while (1) {
        
        // Checagem global de Modo Noturno a cada iteração
        if (g_night_mode) {
            if(current_state != STATE_NIGHT_BLINK) {
                LOG_INF("Entrando no MODO NOTURNO.");
                current_state = STATE_NIGHT_BLINK;
                gpio_pin_set_dt(&led_green, 0); // Apaga verde
                gpio_pin_set_dt(&led_red, 0); // Apaga azul (antes de piscar)
            }
        } else if (current_state == STATE_NIGHT_BLINK) {
            // Saindo do modo noturno
            LOG_INF("Saindo do MODO NOTURNO -> MODO NORMAL");
            current_state = STATE_GREEN; // Volta ao ciclo normal
        }


        switch (current_state) {

        case STATE_NIGHT_BLINK:
            LOG_INF("Modo Noturno: led2 ON");
            gpio_pin_set_dt(&led_red, 1);
            k_sleep(K_SECONDS(1));
            
            if (g_night_mode) { // Checa de novo caso tenha mudado durante o sleep
                LOG_INF("Modo Noturno: led2 OFF");
                gpio_pin_set_dt(&led_red, 0);
                k_sleep(K_SECONDS(1));
            }
            break;

        case STATE_GREEN:
            LOG_INF("Ciclo: VERDE (led0) ON por 4s (ou até botão)");
            gpio_pin_set_dt(&led_red, 0);
            gpio_pin_set_dt(&led_green, 1);
            
            // Limpa qualquer semáforo de botão pendente ANTES de esperar
            k_sem_reset(&button_sem); 

            // Espera pelo semáforo (botão) por no MÁXIMO 4 segundos
            ret = k_sem_take(&button_sem, K_SECONDS(4));

            if (ret == 0) {
                // ret == 0 -> Botão foi pressionado DURANTE o verde.
                LOG_INF("Botão pressionado no VERDE. Prolongando por mais 4s.");
                
                // Não faz nada, apenas deixa o loop repetir.
                // Na próxima iteração, current_state ainda será STATE_GREEN
                // e o timer de 4s será reiniciado.
                
            } else {
                // ret != 0 -> Timeout de 4s esgotou (sem botão).
                LOG_INF("Timeout de 4s (Verde). -> indo para led2 (Azul).");
                
                // Só transiciona se não tiver entrado em modo noturno
                if (!g_night_mode) {
                    gpio_pin_set_dt(&led_green, 0);
                    current_state = STATE_RED;
                }
            }
            break;

        case STATE_RED:
            // **MUDANÇA 1: Duração aumentada para 4s**
            LOG_INF("Ciclo: led2 (Limpeza) ON por 4s (ou até botão)");
            gpio_pin_set_dt(&led_green, 0);
            gpio_pin_set_dt(&led_red, 1);
            
            // Limpa qualquer semáforo de botão pendente ANTES de esperar
            k_sem_reset(&button_sem);

            // **MUDANÇA 1: Duração aumentada para 4s**
            // Espera pelo semáforo (botão) por no MÁXIMO 4 segundos
            ret = k_sem_take(&button_sem, K_SECONDS(4));

            if (ret == 0) {
                // ret == 0 -> Botão foi pressionado
                LOG_INF("Botão pressionado no led2! Ativando delay de 1s...");
                
                // **MUDANÇA 2: Delay de 1s APÓS ativação no vermelho/azul**
                // O led2 (azul) continua aceso durante este sleep.
                k_sleep(K_SECONDS(1));

                LOG_INF("Delay de 1s completo. -> indo para VERDE.");
                
            } else {
                // ret != 0 -> Timeout de 4s esgotou
                LOG_INF("Timeout de 4s (led2). -> indo para VERDE.");
            }
            
            // Em ambos os casos (botão + delay OU timeout), o próximo estado é VERDE
            if (!g_night_mode) {
                gpio_pin_set_dt(&led_red, 0);
                current_state = STATE_GREEN;
            }
            break;
        }
        
        // Pequeno sleep para o scheduler
        k_sleep(K_MSEC(10));
    }
    return 0;
}