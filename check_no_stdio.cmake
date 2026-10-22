# Avoiding stdio - unused - saving some flash with this

set(FORBIDDEN
  __wrap_printf
  __wrap_vprintf
  __wrap_puts
  __wrap_putchar
  stdio_put_string
  weak_raw_printf
  weak_raw_vprintf
)

execute_process(COMMAND "${NM}" "${ELF}" OUTPUT_VARIABLE symbols RESULT_VARIABLE result)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "check_no_stdio: ${NM} failed on ${ELF}")
endif()

set(found "")
foreach(symbol IN LISTS FORBIDDEN)
    if (symbols MATCHES "[ \t]${symbol}\n")
        list(APPEND found ${symbol})
    endif()
endforeach()

if (found)
    get_filename_component(directory "${ELF}" DIRECTORY)
    get_filename_component(stem "${ELF}" NAME_WE)
    file(GLOB images "${directory}/${stem}.bin" "${directory}/${stem}.hex" "${directory}/${stem}.uf2*")
    file(REMOVE "${ELF}" ${images})
    list(JOIN found ", " found)
    message(FATAL_ERROR "${ELF} links printf/stdio (${found}): something calls printf, puts or putchar - use trace() instead. The callers are the 'bl <symbol>' lines in the .dis next to the ELF.")
endif()
