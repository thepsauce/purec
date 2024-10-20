for theme in getcompletion('', 'color')
    execute 'colo ' . theme
    call ConvertToCStruct()
endfor
