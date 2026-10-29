# Física e colisão

Adicione **Physics** pelo Inspector. O seletor **Body Type** oferece:

- **Character Body**: corpo dinâmico afetado pela gravidade, forças e contatos, com
  rotação física travada. A orientação do Transform continua controlada pelo script.
- **Static Body**: massa zero; não cai nem recebe impulsos. Seu Transform define a posição.
- **Rigid Body**: corpo dinâmico com translação e rotação físicas.

Novos componentes usam **Fit to Model**: os limites locais de todas as malhas
fornecem dimensões e centro de referência. Posição, rotação e escala do Transform
(incluindo pais) são aplicadas sobre essa base. Caixa ajusta os limites; esfera,
cápsula, cilindro e cone usam aproximações primitivas desses limites. Esferas usam
um raio conservador quando a escala do objeto é não uniforme. Sem malha, usa-se a
forma unitária abaixo. Cenas antigas mantêm seus tamanhos gravados; marque
**Fit to Model** para ativar o ajuste nelas.

**Collision Shape** seleciona caixa, cápsula (eixo Y), esfera, cilindro (eixo Y),
cone (eixo Y) ou plano infinito (normal Y). Use o plano com Static Body.

**Collider Scale** é um multiplicador local sobre as dimensões de referência e a
escala do Transform; seu padrão é `(1,1,1)`. **Rotation Offset** acrescenta rotação
local em graus (padrão zero). **Collider Offset** acrescenta deslocamento local ao
centro de referência (padrão zero). Esses ajustes não alteram o Transform visual.
Rotações locais sob escalas não uniformes são aproximadas pelos primitivos Bullet;
transformações com cisalhamento não são representadas exatamente.
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


No editor, **View → Show/Hide Colliders (F3)** alterna os contornos na cena. O F3
também funciona no runtime. Verde indica colisão ativa, azul indica trigger e
cinza indica colisor desativado. A opção é temporária e não modifica o projeto.
