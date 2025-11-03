/*
 * Semáforo de Pedestres com Botão, Máquina de Estados e Modo Noturno
 * Placa: FRDM-KL25Z
 *
 * Mapeamento de Pinos:
 * - led0: LED Verde
 * - led1: LED Vermelho (LED Vermelho da placa)
 * - PTA1: Botão de Pedestre (Configurado manualmente)
 *
 * Modo Normal:
 * - Padrão: Vermelho sólido (Não Ande)
 * - Botão Pressionado:
 * 1. Verde (Ande) por 4s
 * 2. Vermelho (Limpeza) por 2s
 * 3. Volta ao Padrão (Vermelho sólido)
 *
 * Modo Noturno:
 * - Verde: Desligado
 * - Vermelho: Pisca (1s ligado, 1s desligado)
 * - Botão: Ignorado
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestre, LOG_LEVEL_DBG);

/* 1. Definição dos LEDs (Usando DT, pois são da placa) */

// Conforme sua especificação de *comentário*: Verde = 0
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
// Usando led1 para o LED Vermelho da placa FRDM-KL25Z
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);


/* 2. NOVO: Definição Manual do Botão (PTA1) - Sem Overlay */

// 2.1. O pino PTA1 está no 'gpioa'
const struct device *gpioa_dev = DEVICE_DT_GET(DT_NODELABEL(gpioa));

// 2.2. O pino é o 1 (para PTA1)
#define BUTTON_PIN 1

// 2.3. Estrutura para o callback (ISR)
static struct gpio_callback button_cb_data;


/* 3. NOVO: Semáforo para sinalizar o evento do botão */
// A ISR (interrupção) do botão vai "dar" (give) este semáforo
// A thread principal vai "pegar" (take)
K_SEM_DEFINE(button_sem, 0, 1); // Inicia "vazio" (0), com limite de 1

/* 4. Flag de Controle do Modo Noturno */
// Mude para 'true' para testar o modo noturno.
static volatile bool g_night_mode = false;

/* 5. NOVO: Estados da Máquina */
enum state {
    STATE_NIGHT_BLINK, // Modo Noturno: Piscando vermelho
    STATE_RED_SOLID,   // Modo Normal: Vermelho sólido (esperando botão)
    STATE_GREEN_CYCLE  // Modo Normal: Ciclo do verde (4s) + vermelho (2s)
};

/*
 * 6. NOVO: Função de Callback (ISR) do Botão
 *
 * Esta função é chamada pela interrupção do GPIO quando o botão é pressionado.
 * É seguro chamar k_sem_give() de dentro de uma ISR.
 */
void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // Só sinaliza se NÃO estiver no modo noturno
    if (!g_night_mode) {
        // Libera o semáforo para a thread main
        k_sem_give(&button_sem);
    }
}

/*
 * 7. Função Main
 *
 * Configura os LEDs, o Botão (com interrupção) e
 * executa a máquina de estados principal.
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

    // Configura o pino PTA1 como entrada com pull-up interno.
    // O botão deve ser conectado entre PTA1 e GND.
    // A flag GPIO_INT_DEBOUNCE foi removida para garantir a compilação.
    ret = gpio_pin_configure(gpioa_dev, BUTTON_PIN, (GPIO_INPUT | GPIO_PULL_UP));
    if (ret < 0) {
        LOG_ERR("Falha ao configurar botão PTA1.");
        return 0;
    }

    // Configura a interrupção para borda de descida (quando o botão é pressionado)
    ret = gpio_pin_interrupt_configure(gpioa_dev, BUTTON_PIN, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar interrupção do botão.");
        return 0;
    }

    // Adiciona a função de callback para a interrupção
    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(BUTTON_PIN));
    gpio_add_callback(gpioa_dev, &button_cb_data);

    /* 7.3. Define o Estado Inicial */
    if (g_night_mode) {
        LOG_INF("Semáforo Iniciado em MODO NOTURNO");
        current_state = STATE_NIGHT_BLINK;
        gpio_pin_set_dt(&led_green, 0); // Verde sempre desligado à noite
    } else {
        LOG_INF("Semáforo Iniciado em MODO NORMAL (Aguardando Botão)");
        current_state = STATE_RED_SOLID;
        gpio_pin_set_dt(&led_green, 0);
        gpio_pin_set_dt(&led_red, 1); // Estado padrão: Vermelho aceso
    }

    /* 7.4. Loop Principal (Máquina de Estados) */
    while (1) {
        switch (current_state) {

        case STATE_NIGHT_BLINK:
            // Lógica do Modo Noturno
            LOG_INF("Modo Noturno: Vermelho ON");
            gpio_pin_set_dt(&led_red, 1);
            k_sleep(K_SECONDS(1));
            
            if (g_night_mode) { // Checa se ainda está em modo noturno
                LOG_INF("Modo Noturno: Vermelho OFF");
                gpio_pin_set_dt(&led_red, 0);
                k_sleep(K_SECONDS(1));
            }

            // Checa se o modo mudou
            if (!g_night_mode) {
                LOG_INF("Mudando para MODO NORMAL.");
                current_state = STATE_RED_SOLID;
                gpio_pin_set_dt(&led_red, 1); // Acende o vermelho
            }
            break;

        case STATE_RED_SOLID:
            // Lógica do Modo Normal (Padrão)
            if (g_night_mode) { // Checa se o modo mudou
                LOG_INF("Mudando para MODO NOTURNO.");
                current_state = STATE_NIGHT_BLINK;
                gpio_pin_set_dt(&led_red, 0); // Apaga o vermelho antes de piscar
                break;
            }

            // Fica "preso" aqui esperando o semáforo ser liberado pela ISR
            LOG_INF("Estado: Vermelho Sólido. Esperando botão...");
            k_sem_take(&button_sem, K_FOREVER);
            
            // Se chegamos aqui, o botão foi pressionado (e não estamos no modo noturno)
            LOG_INF("Botão recebido! Iniciando ciclo verde.");
            current_state = STATE_GREEN_CYCLE;
            break;

        case STATE_GREEN_CYCLE:
            // Executa o ciclo de pedestre
            
            // "imediatamente verde"
            LOG_INF("Ciclo: VERDE (Ande) ON por 4s");
            gpio_pin_set_dt(&led_red, 0);
            gpio_pin_set_dt(&led_green, 1);
            k_sleep(K_SECONDS(4));

            // "continue o ciclo"
            LOG_INF("Ciclo: VERDE OFF");
            gpio_pin_set_dt(&led_green, 0);
            LOG_INF("Ciclo: VERMELHO (Limpeza) ON por 2s");
            gpio_pin_set_dt(&led_red, 1);
            k_sleep(K_SECONDS(2));

            // Ciclo completo, volta ao estado padrão
            LOG_INF("Ciclo completo. Voltando ao estado Vermelho Sólido.");
            
            // Limpa qualquer clique de botão que tenha ocorrido durante o ciclo
            k_sem_reset(&button_sem); 
            
            current_state = STATE_RED_SOLID;
            // O LED vermelho já está aceso, pronto para o estado STATE_RED_SOLID
            break;
        }

        // Pequeno sleep para evitar que o loop rode "solto"
        k_sleep(K_MSEC(10));
    }

    return 0; // Nunca alcançado
}