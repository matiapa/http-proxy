# HTTP Proxy

Este proyecto es un proxy HTTP/1.1 desarrollado para la cátedra de Protocolos de Comunicación del ITBA durante la cursada de 2021-1C.

- [Contenido del repositorio](#contenido-del-repositorio)
- [Instrucciones de compilación](#instrucciones-de-compilación)
- [Instrucciones de uso](#instrucciones-de-uso)
    - [Proxy](#proxy)
    - [Cliente de management](#cliente-de-management)

## Contenido del repositorio

En este repositorio se pueden encontrar los siguientes elementos.

- `src/`: Contiene los archivos de código fuente.
- `Makefile`: Configuración de compilación para la herramienta [make](https://www.gnu.org/software/make/).
- `Informe.pdf`: Informe detallado del proyecto.

## Instrucciones de compilación

Se debe usar la herramienta [make](https://www.gnu.org/software/make/) para generar los ejecutables, los mismos se generarán en la carpeta `/bin`

```bash
> mkdir bin
> make all -s
```

## Instrucciones de uso

#### Proxy
Inicia los servicios de proxy y management.

```bash
> httpd -h
Usage: ./httpd [OPTION]...

   -h                   Imprime la ayuda y termina.
   -l <proxy addr>      Dirección donde servirá el proxy.
   -L <conf  addr>      Dirección donde servirá el servicio de management.
   -p <proxy port>      Puerto entrante del proxy.
   -o <conf port>       Puerto entrante sel servicio de management.
   -N                   Deshabilita los passwords disectors y termina.
   -v                   Imprime información sobre la versión versión y termina.
   
   --doh-ip    <ip>     Dirección del servidor DoH
   --doh-port  <port>   Puerto del servidor DoH
   --doh-host  <host>   Host del servidor DoH
   --doh-path  <host>   Path del servidor DoH

Este proyecto es un proxy HTTP/1.1 desarrollado para la cátedra de Protocolos de Comunicación del ITBA durante la cursada de 2021-1C.
```

### Cliente de management
Utilitario para conectarse al servicio de management del proxy.

```bash
> httpdctl -h
Usage: ./httpdctl [OPTION]...

   -h                      Imprime la ayuda y termina.
   -l <management addr>    Dirección del servicio de management.
   -p <management port>    Puerto del servicio de management.
   -v                      Imprime información sobre la versión versión y termina.
```