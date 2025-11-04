/* Alterações marcadas com comentário "SYNC MOD" */

/*
 * Semáforo de Pedestres com Ciclo Automático e Botão de Interrupção
 * Placa: FRDM-KL25Z
 *
<<<<<<< HEAD
 * Adicionado: sincronização com semáforo de veículos via PTB1 (OUT) / PTB2 (IN)
 * Estratégia de sinal: pulso ativo ALTO (~200ms) em SYNC_OUT (PTB1). Entrada SYNC_IN com pull-down e interrupt rising edge.
=======
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
 * - Ignorado. Não faz absolutamente nada.
 *
 * Modo Noturno:
 * - Verde: Desligado
 * - Vermelho/Azul: Pisca (1s ligado, 1s desligado)
>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
 */


#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestre, LOG_LEVEL_DBG);

<<<<<<< HEAD
/* 1. Definição dos LEDs (Usando DT, pois são da placa) */

// Verde = led0
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
// Vermelho = led1
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);


/* 2. Definição Manual do Botão (PTA1) - Sem Overlay */

=======
/* 1. Definição dos LEDs (Usando DT) */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
// Usando 'led2' (LED AZUL) conforme o seu código original
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);


/* 2. Definição Manual do Botão (PTA1) - Sem Overlay */
>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
const struct device *gpioa_dev = DEVICE_DT_GET(DT_NODELABEL(gpioa));
#define BUTTON_PIN 1
static struct gpio_callback button_cb_data;

<<<<<<< HEAD
/* 3. Semáforo para sinalizar o evento do botão */
K_SEM_DEFINE(button_sem, 0, 1); // Liberado pela ISR do botão (ou pela lógica de envio)
=======

/* 3. Semáforo para sinalizar o evento do botão */
K_SEM_DEFINE(button_sem, 0, 1); // Inicia "vazio" (0, com limite de 1

>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5

/* 4. Flag de Controle do Modo Noturno */
static volatile bool g_night_mode = false;

<<<<<<< HEAD
=======

>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
/* 5. Estados da Máquina */
enum state {
    STATE_GREEN,
    STATE_RED,
    STATE_NIGHT_BLINK
};

<<<<<<< HEAD
/* =========================
   SYNC (SINCRONIZAÇÃO) - MODIFICAÇÕES
   =========================
   - SYNC_OUT : PTB1 (saída digital)
   - SYNC_IN  : PTB2 (entrada com interrupção, pull-down)
   - Estratégia: pulso ativo ALTO (~200ms). Entrada com pull-down detecta borda de subida.
   - Quando o botão é pressionado, a placa pedestre envia um pulso em SYNC_OUT para a placa veículos
     e **aguarda** um pulso de confirmação em SYNC_IN antes de iniciar seu ciclo verde.
   - Em modo noturno, a sincronização é ignorada.
*/

/* SYNC on PORTB */
#define SYNC_PORT   DT_NODELABEL(gpiob)  /* SYNC MOD: porta B */
#define SYNC_OUT_PIN 1                    /* PTB1 - saída (gera pulsos) */
#define SYNC_IN_PIN  2                    /* PTB2 - entrada com interrupção */

static const struct device *sync_dev = DEVICE_DT_GET(SYNC_PORT);
static struct gpio_callback sync_in_cb_data;

/* Semáforo que a ISR SYNC_IN dá quando recebe confirmação (placa veículos confirma seguro) */
K_SEM_DEFINE(sync_confirm_sem, 0, 1);

/* Envia um pulso ativo (HIGH) por ~200ms no SYNC_OUT (bloqueante curto) */
static void send_sync_pulse(void)
{
    if (!device_is_ready(sync_dev)) {
        LOG_ERR("SYNC: PORTB não pronto para enviar pulso.");
        return;
    }

    if (g_night_mode) {
        LOG_DBG("SYNC: modo noturno ativo - não envia pulso.");
        return;
    }

    gpio_pin_set(sync_dev, SYNC_OUT_PIN, 1);
    k_msleep(200); /* pulso de 200ms */
    gpio_pin_set(sync_dev, SYNC_OUT_PIN, 0);
    LOG_INF("SYNC: pulso enviado em SYNC_OUT (PTB1).");
}

/* Callback da entrada SYNC_IN (recebe confirmação da placa veículos) */
void sync_in_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(dev);

    if (g_night_mode) {
        LOG_DBG("SYNC: sinal recebido mas ignorado (modo noturno)");
        return;
    }

    /* Da ISR, apenas sinalizamos o semáforo de confirmação */
    k_sem_give(&sync_confirm_sem);
    LOG_INF("SYNC: confirmação recebida (SYNC_IN).");
}

/* =========================
   Fim das modificações SYNC
   ========================= */


/*
 * Função de Callback (ISR) do Botão
 *
 * Agora a ISR somente dá o semáforo button_sem; a thread principal fará:
 *  - enviar pulso para veículo via SYNC_OUT
 *  - aguardar confirmação via sync_confirm_sem
 *  - só então iniciar ciclo verde
 */
void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    /* Só sinaliza se NÃO estiver no modo noturno */
=======

/*
 * 6. Função de Callback (ISR) do Botão
 */
void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // A ISR apenas sinaliza. A lógica de "o que fazer"
    // depende do estado atual (STATE_GREEN ou STATE_RED).
>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
    if (!g_night_mode) {
        k_sem_give(&button_sem);
    }
}

<<<<<<< HEAD
=======

/*
 * 7. Função Main (Controladora da Máquina de Estados)
 */
>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
int main(void)
{
    int ret;
    enum state current_state;

    /* LEDs via DT */
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("Dispositivo de LED não está pronto.");
        return 0;
    }
    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;
    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;

    /* Configuração do Botão (Manual, sem DT/Overlay) */
    if (!device_is_ready(gpioa_dev)) {
        LOG_ERR("Dispositivo GPIOA (PTA1) não está pronto.");
        return 0;
    }
<<<<<<< HEAD
=======

    // Configura o pino PTA1 como entrada com pull-up interno
>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
    ret = gpio_pin_configure(gpioa_dev, BUTTON_PIN, (GPIO_INPUT | GPIO_PULL_UP));
    if (ret < 0) {
        LOG_ERR("Falha ao configurar botão PTA1.");
        return 0;
    }
<<<<<<< HEAD
=======

    // Configura a interrupção 
>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
    ret = gpio_pin_interrupt_configure(gpioa_dev, BUTTON_PIN, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar interrupção do botão.");
        return 0;
    }
    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(BUTTON_PIN));
    gpio_add_callback(gpioa_dev, &button_cb_data);

    /* SYNC MOD: configurar SYNC_OUT e SYNC_IN em PORTB */
    if (!device_is_ready(sync_dev)) {
        LOG_ERR("SYNC: PORTB não pronto. Sincronização desabilitada.");
        /* prosseguimos sem sync (mas o código assume sync_dev pronto para testes) */
    } else {
        ret = gpio_pin_configure(sync_dev, SYNC_OUT_PIN, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("Falha ao configurar SYNC_OUT PTB1: %d", ret);
        }
        /* SYNC_IN: entrada com pull-down e interrupção na borda de subida (active HIGH pulse) */
        ret = gpio_pin_configure(sync_dev, SYNC_IN_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
        if (ret < 0) {
            LOG_ERR("Falha ao configurar SYNC_IN PTB2: %d", ret);
        } else {
            ret = gpio_pin_interrupt_configure(sync_dev, SYNC_IN_PIN, GPIO_INT_EDGE_TO_ACTIVE);
            if (ret < 0) {
                LOG_ERR("Falha ao configurar interrupção SYNC_IN: %d", ret);
            } else {
                gpio_init_callback(&sync_in_cb_data, sync_in_callback, BIT(SYNC_IN_PIN));
                gpio_add_callback(sync_dev, &sync_in_cb_data);
                LOG_INF("SYNC: configurado PTB1 (OUT) / PTB2 (IN int)");
            }
        }
    }

    /* Estado Inicial */
    if (g_night_mode) {
        LOG_INF("Semáforo Iniciado em MODO NOTURNO");
        current_state = STATE_NIGHT_BLINK;
        gpio_pin_set_dt(&led_green, 0); 
    } else {
        LOG_INF("Semáforo Iniciado em MODO NORMAL (Ciclo Automático)");
        current_state = STATE_GREEN; // Inicia no Verde
        gpio_pin_set_dt(&led_red, 0); // Garante que o azul comece apagado
    }

    /* Loop Principal (Máquina de Estados) */
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

<<<<<<< HEAD
        case STATE_RED_SOLID:
            // Lógica do Modo Normal (Padrão)
            if (g_night_mode) { // Checa se o modo mudou
                LOG_INF("Mudando para MODO NOTURNO.");
                current_state = STATE_NIGHT_BLINK;
                gpio_pin_set_dt(&led_red, 0); // Apaga o vermelho antes de piscar
                break;
            }

            // Espera botão (ISR libera button_sem)
            LOG_INF("Estado: Vermelho Sólido. Esperando botão...");
            k_sem_take(&button_sem, K_FOREVER);

            // Se chegamos aqui, o botão foi pressionado (e não estamos no modo noturno)
            LOG_INF("Botão recebido! Enviando pedido ao semáforo de veículos (SYNC_OUT) e aguardando confirmação...");
            /* SYNC MOD: enviar pulso para a placa de veículos solicitando travessia */
            send_sync_pulse();

            /* Aguardar confirmação da placa de veículos via SYNC_IN (semáforo sync_confirm_sem) */
            LOG_INF("Aguardando confirmação do semáforo de veículos (SYNC_IN)...");
            k_sem_take(&sync_confirm_sem, K_FOREVER); /* Bloqueia até receber confirmação */
            LOG_INF("Confirmação recebida! Iniciando ciclo verde.");

            current_state = STATE_GREEN_CYCLE;
            break;

        case STATE_GREEN_CYCLE:
            // Executa o ciclo de pedestre
            
            // "imediatamente verde"
            LOG_INF("Ciclo: VERDE (Ande) ON por 4s");
=======
        // ***** INÍCIO DA ALTERAÇÃO *****
        case STATE_GREEN:
            LOG_INF("Ciclo: VERDE (led0) ON por 4s");
>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
            gpio_pin_set_dt(&led_red, 0);
            gpio_pin_set_dt(&led_green, 1);
            
            // Dorme por 4s (Não pode ser interrompido pelo botão)
            // A ISR do botão ainda vai disparar e dar k_sem_give(),
            // mas esse semáforo será limpo no início do STATE_RED.
            k_sleep(K_SECONDS(4)); 
            
            LOG_INF("Timeout de 4s (Verde). -> indo para led2 (Azul).");

            // Só transiciona se não tiver entrado em modo noturno
            if (!g_night_mode) {
                gpio_pin_set_dt(&led_green, 0);
                current_state = STATE_RED;
            }
            break;
        // ***** FIM DA ALTERAÇÃO *****

        case STATE_RED:
            LOG_INF("Ciclo: led2 (Limpeza) ON por 4s (ou até botão)");
            gpio_pin_set_dt(&led_green, 0);
            gpio_pin_set_dt(&led_red, 1);
            
            // Limpa qualquer semáforo de botão pendente
            // (Isso inclui cliques que ocorreram durante o STATE_GREEN)
            k_sem_reset(&button_sem);

            // Espera pelo semáforo (botão) por no MÁXIMO 4 segundos
            ret = k_sem_take(&button_sem, K_SECONDS(4));

            if (ret == 0) {
                // ret == 0 -> Botão foi pressionado
                LOG_INF("Botão pressionado no led2! Ativando delay de 1s...");
                
                // Delay de 1s APÓS ativação no vermelho/azul
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
<<<<<<< HEAD

    return 0; // Nunca alcançado
}
=======
    return 0;
}
>>>>>>> 965aeb510df337c18aa049fcc7d28c009aeb6db5
