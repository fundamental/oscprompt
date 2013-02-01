#define MAX_SNARF 10000
char snarf_buffer[MAX_SNARF];
char snarf_path[128];

extern Ports *backend_ports;

void barf(void)
{
    unsigned elms = rtosc_bundle_elements(snarf_buffer, -1);
    for(unsigned i=0; i<elms; ++i)
        backend_ports->dispatch(NULL, 0, rtosc_bundle_fetch(snarf_buffer,i)+1, NULL);
}

bool snarf_p(const char *meta)
{
    while(*meta && *meta != ':') ++meta;
    ++meta;
    while(*meta && *meta != ':')
        if(*meta++ == 's')
            return true;

    return false;
}

void scat(char *dest, const char *src)
{
    while(*dest) dest++;
    if(*dest) dest++;
    while(*src && *src!=':') *dest++ = *src++;
    *dest = 0;
}

void snarf_port(const char *port)
{
    char message_buf[1024];
    //Load the right address
    ::scat(snarf_path, port);

    //Snarf it
    rtosc_message(message_buf, 1024, snarf_path, "N");

    backend_ports->dispatch(NULL, 0, message_buf+1, NULL);

    //Clear out the port
    char *buf = rindex(snarf_path, '/')+1;
    while(*buf) *buf++ = 0;
}

void snarf_ports(Ports *ports);

bool special(const char *name)
{
    return index(name,'#');
}

void ensure_path(char *name)
{
    if(rindex(name, '/')[1] != '/')
        strcat(name, "/");
}

void magic(const char *name, Ports *ports)
{
    char *old_end = rindex(snarf_path, '/')+1;
    char *pos = old_end;
    while(*name != '#') *pos++ = *name++;
    unsigned max = atoi(name+1);
    for(int i=0; i<max; ++i)
    {
        sprintf(pos,"%d",i);
        ensure_path(snarf_path);
        snarf_ports(ports);//Snarf
    }
    while(*old_end) *old_end++=0; //Erase
}

void snarf_ports(Ports *ports)
{
    for(const Port &p : *ports) {
        if(index(p.name, '/')) {//it is another tree
            if(special(p.name)) {
                magic(p.name, p.ports);
            } else {
                char *old_end = rindex(snarf_path, '/')+1;
                ::scat(snarf_path, p.name);//Cat

                snarf_ports(p.ports);//Snarf

                while(*old_end) *old_end++=0; //Erase
            }
        } else
            snarf_port(p.name);
    }
}

void snarf_addf(float f)
{
    unsigned len = rtosc_message_length(snarf_buffer, -1);
    unsigned msg_len = rtosc_message(snarf_buffer+len, MAX_SNARF-len, snarf_path, "f", f);
    *(uint32_t*)(snarf_buffer+len-4) = msg_len;
}

void snarf_addi(int i)
{
    unsigned len = rtosc_message_length(snarf_buffer, -1);
    unsigned msg_len = rtosc_message(snarf_buffer+len, MAX_SNARF-len, snarf_path, "i", i);
    *(uint32_t*)(snarf_buffer+len-4) = msg_len;
}

void snarf(void)
{
    memset(snarf_buffer, 0, sizeof(snarf_buffer));
    memset(snarf_path,   0, sizeof(snarf_path));
    strcpy(snarf_buffer, "#bundle");
    snarf_path[0] = '/';
    snarf_ports(backend_ports);
}
