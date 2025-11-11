Ótimo — seguindo o **Modelo-V** do link que você enviou, abaixo estão **propostas de testes** organizadas conforme solicitado. Mantive o padrão usado no material: testes de componentes individuais, depois integração e depois requisito especial (modo noturno).
Cada item está descrito de forma clara, objetiva e mensurável para fácil aplicação em bancada.

---

## ✅ **1. Testes de Código Individual — Semáforo de Pedestres (Mestre)**

### **Teste P1 – Ciclo básico sem interação do usuário**

**Objetivo:** Verificar se a máquina de estados do Mestre executa o ciclo completo corretamente sem pressionar o botão.
**Passos:**

1. Energizar a placa do Pedestre (Mestre) isoladamente, sem conectar com o Escravo.
2. Observar os LEDs por um ciclo completo.
   **Resultado Esperado:**

* LED Vermelho ON por ~3s
* LED Vermelho ON + (MASTER_SIGNAL_PIN = 1) por 1s
* LED Verde ON por 4s
* Retorna ao início
  **Critério de Sucesso:** Duração aproximada do ciclo corresponde aos tempos programados (3s, 1s, 4s) durante pelo menos 3 ciclos consecutivos.

---

### **Teste P2 – Reação ao acionamento do botão**

**Objetivo:** Validar que o botão de pedestre interrompe corretamente o estado “Veículo Verde”.
**Passos:**

1. Reiniciar o Mestre e aguardar entrar no **Veículo Verde** (LED vermelho aceso no pedestre).
2. Pressionar o botão antes de completar 3 segundos.
   **Resultado Esperado:**

* Log registra a interrupção (“Botão pressionado! Interrompendo…”).
* O estado muda imediatamente para **Veículo Amarelo**.
  **Critério de Sucesso:** A transição ocorre imediatamente (≤200ms após acionamento).

---

## 🚗 **2. Testes de Código Individual — Semáforo de Veículos (Escravo)**

### **Teste V1 – Reação a borda de subida no pino Mestre**

**Objetivo:** Confirmar que o Escravo reconhece corretamente a transição 0→1 no pino de entrada e executa amarelo e depois vermelho.
**Passos:**

1. Alimentar somente o Escravo (desconectado do Mestre).
2. Aplicar manualmente nível lógico LOW no pino e depois gerar uma borda de subida para HIGH.
3. Monitorar LEDs.
   **Resultado Esperado:**

* AMARELO por 1s (ambos LEDs ON)
* Após 1s, apenas o LED Vermelho permanece ON
  **Critério de Sucesso:** Tempos e sequência devem ser exatamente: 1s amarelo → vermelho estável.

---

### **Teste V2 – Reação a borda de descida no pino Mestre**

**Objetivo:** Confirmar comportamento ao receber transição HIGH→LOW.
**Passos:**

1. Com o pino em HIGH e LED vermelho aceso, gerar borda de descida para LOW.
2. Monitorar LEDs.
   **Resultado Esperado:**

* LED Verde acende imediatamente
  **Critério de Sucesso:** LED vermelho apaga, LED verde acende em até 200ms.

---

## 🔀 **3. Testes de Integração — Mestre + Escravo + Botão**

### **Teste I1 – Sincronismo completo do ciclo**

**Objetivo:** Validar que Mestre e Escravo permanecem sincronizados no ciclo completo (sem interferência do botão).
**Passos:**

1. Conectar os dois dispositivos.
2. Rodar por 3 ciclos completos.
   **Resultado Esperado:**

* Quando Pedestre Vermelho → Veículo Verde
* Quando Pedestre prepara transição → Veículo Amarelo
* Quando Pedestre Verde → Veículo Vermelho
  **Critério de Sucesso:** Nos 3 ciclos, não ocorre quebra de sincronismo (nenhum LED fora do estado compatível).

---

### **Teste I2 – Pressionar botão e verificar sincronismo**

**Objetivo:** Avaliar se o comando do pedestre interrompe corretamente o ciclo e o Escravo acompanha.
**Passos:**

1. Durante Veículo Verde, apertar o botão.
2. Observar Mestre e Escravo.
   **Resultado Esperado:**

* Mestre vai para amarelo imediatamente
* Escravo também entra em amarelo por 1s, depois vermelho
* Pedestre recebe verde apenas após Escravo estar vermelho
  **Critério de Sucesso:** Sincronismo mantido durante a interrupção.

---

## 🌙 **4. Testes do Modo Noturno**

### **Teste N1 – Ativação e permanência no modo noturno**

**Objetivo:** Verificar se, ao ativar `g_night_mode = true`, o semáforo entra no comportamento piscante corretamente.
**Passos:**

1. Ativar `g_night_mode = true` no Mestre e Escravo (pode ser via variável, shell ou breakpoint).
2. Observar LEDs por pelo menos 20 segundos.
   **Resultado Esperado (em cada dispositivo):**

* Ambos LEDs piscam 2s ON / 2s OFF continuamente
* Semáforo normal fica 100% pausado
  **Critério de Sucesso:** Frequência e duty cycle mantidos (2s±0.1 ON / 2s±0.1 OFF).

---

### **Teste N2 – Saída do modo noturno e retomada do ciclo normal**

**Objetivo:** Garantir que o sistema retoma operação normal sem travamentos após sair do modo noturno.
**Passos:**

1. Ativar o modo noturno por ≥10s.
2. Desativar `g_night_mode = false`.
3. Observar retomada do ciclo.
   **Resultado Esperado:**

* LEDs apagam momentaneamente (clear) e o sistema retorna ao início do ciclo.
* Mestre e Escravo reagem novamente ao botão e ao sinal mestre.
  **Critério de Sucesso:** Retorno ao modo normal em até 3s, sem estados inconsistentes.

---

Se quiser, posso complementar com:

✅ **casos de teste negativos** (ex.: ruído no pino, bouncing do botão)
✅ **testes automatizados com Zephyr ztest**
✅ **testes de estresse e robustez (overnight test)**

---

Quer que eu transforme esses testes em uma **tabela no padrão do Modelo-V** (com ID, propósito, entradas, execução, resultados esperados e critérios de aceitação)?
