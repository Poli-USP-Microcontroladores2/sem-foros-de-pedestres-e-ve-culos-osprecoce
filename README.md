# **📘 Relatório de Resultados de Testes – Sistema de Semáforo Mestre/Escravo com Modo Noturno**

### Projeto: Semáforo de Pedestres (Mestre) e Veículos (Escravo) – Zephyr RTOS  
### Plataforma: FRDM-KL25Z  
### Versão Firmware: Com Modo Noturno Implementado  
### Status Geral: ✅ **Todos os testes aprovados**

---

## ✅ 1. Testes Individuais – Semáforo de Pedestres (Mestre)

### **Teste P1 – Ciclo básico sem interação**
**Objetivo:** Verificar se o semáforo do Pedestre executa o ciclo completo corretamente sem acionamento do botão.  
**Resultado:**  
O ciclo ocorreu normalmente em todos os testes:  
- LED Vermelho por ~3s  
- Transição para amarelo (via sinal HIGH no pino) por ~1s  
- LED Verde por ~4s  
A lógica repetiu por múltiplos ciclos conforme esperado.  

**Status:** ✅ Aprovado  
**Conclusão:** A máquina de estados do Mestre funciona adequadamente de forma autônoma.

---

### **Teste P2 – Reação ao botão**
**Objetivo:** Validar interrupção do estado “Veículo Verde” com o botão.  
**Resultado:**  
Ao pressionar o botão durante o período de 3s do “Veículo Verde”, a transição para o estado de “Veículo Amarelo” ocorreu imediatamente (<200ms).  
Os logs confirmaram o evento, e o ciclo prosseguiu normalmente.  

**Status:** ✅ Aprovado  
**Conclusão:** O botão está respondendo corretamente e interruptivamente.

---

## 🚗 2. Testes Individuais – Semáforo de Veículos (Escravo)

### **Teste V1 – Borda de subida no sinal do Mestre**
**Objetivo:** Confirmar a reação ao comando HIGH do Mestre.  
**Resultado:**  
Ao produzir a borda de subida (0→1) no pino, o Escravo acionou corretamente:  
- Amarelo (LED verde + vermelho ON) por 1s  
- Em seguida, LED Vermelho permaneceu aceso  
O tempo foi medido e estava dentro do esperado.  

**Status:** ✅ Aprovado  
**Conclusão:** Detecção de borda de subida e transição para amarelo/vermelho está correta.

---

### **Teste V2 – Borda de descida no sinal do Mestre**
**Objetivo:** Confirmar a reação ao comando LOW do Mestre.  
**Resultado:**  
Ao gerar a borda de descida (1→0), o Escravo alternou imediatamente para LED Verde.  
Não houve instabilidades, e não foram observados estados incorretos.  

**Status:** ✅ Aprovado  
**Conclusão:** A reação à borda de descida está funcionando corretamente, indicando sincronismo adequado.

---

## 🔀 3. Testes de Integração – Mestre + Escravo + Botão

### **Teste I1 – Sincronismo completo**
**Objetivo:** Confirmar sincronismo entre o ciclo do Mestre e do Escravo.  
**Resultado:**  
Ambos os dispositivos executaram três ciclos completos de forma perfeitamente sincronizada.  
Estados observados foram compatíveis em todos os instantes (ex.: pedestre verde ↔ veículo vermelho).  

**Status:** ✅ Aprovado  
**Conclusão:** A integração entre Mestre e Escravo está funcional e estável.

---

### **Teste I2 – Interrupção com botão e sincronismo**
**Objetivo:** Garantir que o botão interrompe o ciclo e o Escravo acompanha corretamente.  
**Resultado:**  
Ao apertar o botão:  
- Mestre mudou de Veículo Verde para Amarelo imediatamente  
- Escravo acompanhou a mudança com tempo correto  
- Após 1s, ambos atingiram vermelho e depois verde para pedestre  
Não houve perda de sincronismo.  

**Status:** ✅ Aprovado  
**Conclusão:** A integração entre interrupção, Mestre e Escravo está totalmente funcional.

---

## 🌙 4. Testes do Modo Noturno

### **Teste N1 – Ativação e funcionamento contínuo**
**Objetivo:** Verificar comportamento do modo noturno.  
**Resultado:**  
Após ativar o modo noturno, tanto o Mestre quanto o Escravo passaram a piscar “amarelo” a cada 2s ON / 2s OFF.  
O comportamento persistiu corretamente por mais de 20s, sem falhas ou travamentos.  

**Status:** ✅ Aprovado  
**Conclusão:** O modo noturno cumpre o requisito de piscar amarelo continuamente.

---

### **Teste N2 – Saída e retorno ao modo normal**
**Objetivo:** Garantir retorno correto ao modo normal após desativação do modo noturno.  
**Resultado:**  
Ao desativar o modo noturno:  
- Ambos os dispositivos cessaram o piscar corretamente  
- Retornaram ao ciclo normal em até 3 segundos  
- Sincronismo foi restabelecido sem inconsistências  

**Status:** ✅ Aprovado  
**Conclusão:** O sistema retorna ao funcionamento normal de forma confiável após o modo noturno.

---

## 🏁 **Resumo Final**

| Categoria Testada | Situação |
|-------------------|-----------|
| Código Pedestre (Mestre) | ✅ Aprovado |
| Código Veículos (Escravo) | ✅ Aprovado |
| Integração com Botão | ✅ Aprovado |
| Modo Noturno | ✅ Aprovado |

### 📍 **Resultado Geral:**  
✅ **Todos os requisitos foram validados com sucesso e o sistema está aprovado para integração final.**
