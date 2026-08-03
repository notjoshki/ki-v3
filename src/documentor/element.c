#include "element.h"
#include "list.h"

void delete_element(Element *element) {
    delete_list(&element->comments);
}