SECTIONS
{
    mqtt_commands : ALIGN(8)
    {
        __start_mqtt_commands = .;
        KEEP(*(mqtt_commands))
        __stop_mqtt_commands = .;
    }

    ws_commands : ALIGN(8)
    {
        __start_ws_commands = .;
        KEEP(*(ws_commands))
        __stop_ws_commands = .;
    }

    telegram_commands : ALIGN(8)
    {
        __start_telegram_commands = .;
        KEEP(*(telegram_commands))
        __stop_telegram_commands = .;
    }

    calendar_commands : ALIGN(8)
    {
        __start_calendar_commands = .;
        KEEP(*(calendar_commands))
        __stop_calendar_commands = .;
    }

    ai_commands : ALIGN(8)
    {
        __start_ai_commands = .;
        KEEP(*(ai_commands))
        __stop_ai_commands = .;
    }

    ai_context_commands : ALIGN(8)
    {
        __start_ai_context_commands = .;
        KEEP(*(ai_context_commands))
        __stop_ai_context_commands = .;
    }
} INSERT AFTER .data.rel.ro;
