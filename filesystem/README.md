# filesystem/ - conteudo a embutir na imagem

**Vazio na versao 0.2.** O sistema de arquivos da 0.2 vive na RAM (`kernel/ramfs.c`) e sua arvore
inicial e criada pelo proprio kernel a cada boot:

    /
    ├── bin/
    ├── system/
    ├── home/user/leia-me.txt
    ├── etc/hostname, etc/motd
    ├── tmp/
    ├── apps/
    └── lib/

Este diretorio fica reservado para o conteudo que sera embutido na imagem e carregado no ramfs
quando houver o que carregar (a partir da 0.3, scripts e aplicativos Ruby).
