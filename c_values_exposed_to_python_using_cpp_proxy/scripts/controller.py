from cppbridge import cpp

MESSAGE = "THE MATRIX HAS YOU"


def update_values():
    # Read values
    row = cpp.row
    max_rows = cpp.max_rows
    reveal = cpp.reveal

    # If still revealing characters
    if reveal < len(MESSAGE):
        reveal += 1
    else:
        # Fully revealed → scroll downward
        row += 1
        # If the top of the column goes off-screen → reset
        if row >= max_rows:
            row = 0
            reveal = 1

    # Modify them
    cpp.row = row
    cpp.reveal = reveal
    cpp.message = MESSAGE
