function! ConvertToCStruct()
    let theme_name = g:colors_name

    let term_colors = []
    let attribs = {}

    let term_color_map = {}
    let color_counter = 0

    let highlight_groups = {
                \ 'HI_NORMAL': 'Normal',
                \ 'HI_COMMENT': 'Comment',
                \ 'HI_JAVADOC': 'Identifier',
                \ 'HI_TYPE': 'Type',
                \ 'HI_TYPE_MOD': 'StorageClass',
                \ 'HI_IDENTIFIER': 'Identifier',
                \ 'HI_STATEMENT': 'Statement',
                \ 'HI_PREPROC': 'PreProc',
                \ 'HI_OPERATOR': 'Operator',
                \ 'HI_ERROR': 'Error',
                \ 'HI_NUMBER': 'Number',
                \ 'HI_STRING': 'String',
                \ 'HI_CHAR': 'Character',
                \ 'HI_FUNCTION': 'Function',
                \ 'HI_LINE_NO': 'LineNr',
                \ 'HI_STATUS': 'StatusLine',
                \ 'HI_VERT_SPLIT': 'VertSplit',
                \ 'HI_PAREN_MATCH': 'MatchParen',
                \ 'HI_FUZZY': 'PmenuSel',
                \ 'HI_CMD': 'CmdLine',
                \ 'HI_ADDED': 'DiffAdd',
                \ 'HI_REMOVED': 'DiffDelete',
                \ 'HI_CHANGED': 'DiffChange',
                \ 'HI_SEARCH': 'Search',
                \ 'HI_VISUAL': 'Visual',
                \ 'HI_TODO': 'Todo'
                \ }

    for [c_struct_name, hl_group] in items(highlight_groups)
        let hl_id = synIDtrans(hlID(hl_group))

        let fg_hex = synIDattr(hl_id, "fg#")
        let bg_hex = synIDattr(hl_id, "bg#")

        if fg_hex == ""
            let fg_hex = synIDattr(hlID("Normal"), "fg#")
        endif

        if bg_hex == ""
            let bg_hex = synIDattr(hlID("Normal"), "bg#")
        endif

        if fg_hex == ""
            let fg_hex = "#ffffff"
        endif

        if bg_hex == ""
            let bg_hex = "#000000"
        endif

        if !has_key(term_color_map, fg_hex)
            let term_color_map[fg_hex] = color_counter
            call add(term_colors, fg_hex)
            let color_counter += 1
        endif

        if !has_key(term_color_map, bg_hex)
            let term_color_map[bg_hex] = color_counter
            call add(term_colors, bg_hex)
            let color_counter += 1
        endif

        let fg_index = term_color_map[fg_hex]
        let bg_index = term_color_map[bg_hex]

        let attr = ""
        if synIDattr(hl_id, "bold", "cterm") == "1"
            let attr = "A_BOLD"
        endif
        if synIDattr(hl_id, "italic", "cterm") == "1"
            if attr != ""
                let attr = attr . " | "
            endif
            let attr = attr . "A_ITALIC"
        endif
        if synIDattr(hl_id, "underline", "cterm") == "1"
            if attr != ""
                let attr = attr . " | "
            endif
            let attr = attr . "A_UNDERLINE"
        endif
        if synIDattr(hl_id, "reverse", "cterm") == "1"
            if attr != ""
                let attr = attr . " | "
            endif
            let attr = attr . "A_REVERSE"
        endif

        if attr == ""
            let attr = "0"
        endif

        let attribs[c_struct_name] = printf('{ %d, %d, %s }', fg_index, bg_index, attr)
    endfor

    let lines = [
                \ '    {',
                \ '        .name = "' . theme_name . '",',
                \ '        .term_colors = {'
                \ ]
    for color in term_colors
        call add(lines, '            "' . color . '",')
    endfor
    call add(lines, '        },')
    call add(lines, '        .attribs = {')
    for [name, _] in items(highlight_groups)
        call add(lines, '            [' . name . '] = ' . attribs[name] . ',')
    endfor
    call add(lines, '        },')
    call add(lines, '        .colors_needed = ' . color_counter . ',')
    call add(lines, '    },')
    call add(lines, '')

    call writefile(lines, "out.txt", "a")
endfunction
