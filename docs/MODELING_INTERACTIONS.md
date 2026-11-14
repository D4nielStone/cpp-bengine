# Composição e ferramentas de modelagem

Na área **Modelagem**, selecione um objeto com modelo `.bmodel` editável.

- Em **Malhas → Adicionar primitiva**, adicione cubos, esferas, planos, cilindros ou cápsulas. A geometria entra na malha ativa, preservando o restante do objeto. Cada adição cria um grupo de faces selecionável; a nova peça fica selecionada para mover, girar ou escalar com o gizmo. Os grupos permitem selecionar novamente cada peça. As peças compartilham o material da malha ativa.
- Em **Faces**, selecione a face e clique em **Extrudar face**. Arraste a seta laranja e solte para confirmar. A seta segue a normal média da seleção; quando vista de frente, usa um controle vertical. `E` inicia a prévia pelo mouse e confirma com clique. `Esc` ou botão direito cancela toda a extrusão, inclusive a topologia criada.
- Clique em **Loop Cut** ou pressione `Ctrl+R`, depois clique na **face do objeto**. Duas linhas coloridas mostram as direções locais possíveis. Clique na linha desejada, mova o mouse para deslizar e clique novamente para confirmar. Botão direito centraliza; `Esc` cancela. Não é necessário selecionar uma aresta. O corte percorre o loop conectado de faces quadriláteras; faces triangulares ou topologia incompatível são recusadas.

As alterações são locais ao editor até salvar o `.bmodel`. Adição, extrusão e corte confirmado entram no histórico de desfazer/refazer. Em modelos riggados, ative a edição de repouso e pesos antes de modificar a geometria.
