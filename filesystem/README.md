# filesystem/ - arquivos extras embutidos na imagem

Tudo o que for colocado aqui (exceto este README) e embutido por `tools/embed.rb` e copiado para o ramfs a
cada boot, no mesmo caminho: `filesystem/etc/exemplo.txt` vira `/etc/exemplo.txt`. Um arquivo com o mesmo
caminho de um da arvore inicial o substitui.

Os programas Ruby ficam em `ruby/` (ver `ruby/README.md`); esta pasta e para dados e textos.
Vazia na 0.3.
