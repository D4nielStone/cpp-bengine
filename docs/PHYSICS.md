# Física e colisão

Adicione **Physics** pelo Inspector. O seletor **Body Type** oferece:

- **Character Body**: corpo dinâmico afetado pela gravidade, forças e contatos, com
  rotação física travada. A orientação do Transform continua controlada pelo script.
- **Static Body**: massa zero; não cai nem recebe impulsos. Seu Transform define a posição.
- **Rigid Body**: corpo dinâmico com translação e rotação físicas.

**Collision Shape** seleciona caixa, cápsula (eixo Y), esfera, cilindro (eixo Y),
cone (eixo Y) ou plano infinito (normal Y). Use o plano com Static Body.

**Collider Scale** multiplica as dimensões locais da forma e a escala do Transform.
As formas unitárias têm: caixa/cilindro com dimensões 1×1×1, esfera de raio 0,5,
cone de raio 0,5 e altura 1, cápsula de raio 0,5 e trecho cilíndrico de altura 1,8
(altura total 2,8). As dimensões devem ser positivas. Esferas usam escala uniforme;
para elas, ajuste os três eixos igualmente. O plano é infinito e não possui tamanho.
**Collider Offset** desloca o colisor em coordenadas locais da entidade, também
sujeitas à sua escala e rotação.

**Collision Enabled** desativa contatos e consultas por raio quando desmarcado,
sem desativar a gravidade. **Trigger** mantém a detecção de contatos no Bullet,
mas elimina a resposta que bloqueia os corpos. Esta alteração não adiciona callbacks
Lua de entrada/saída de trigger. **Mass**, **Friction** e **Restitution** controlam
massa, atrito e restituição. Alterar a massa de zero para positiva converte um corpo
estático em dinâmico; massa zero torna o corpo estático, preservando a API existente.

Todos os campos são salvos na cena. Arquivos antigos continuam aceitos com suas
formas, massa e material existentes; colisão começa ativada, sem trigger, com
escala unitária e deslocamento zero. Colisão por malha não está incluída.

No exemplo FPP, PlayerCapsule usa Character Body com cápsula deslocada para cima
em relação aos pés. O script controla a velocidade horizontal, preservando a
velocidade vertical da gravidade. Chão e paredes usam Static Body e colisores
ajustados às dimensões das malhas do cenário.
