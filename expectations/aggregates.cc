#include <assert.h>
#include "aggregates.h"
#include "expect.tab.hh"

Aggregate::Aggregate(Node *_node) {
	assert(_node->type() == NODE_OPERATOR);
	OperatorNode *onode = (OperatorNode*)_node;
	assert(onode->nops() == 1);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	node = onode->operands[0];
}

void Aggregate::print(FILE *fp) const {
	fprintf(fp, "<assert>\n");
	print_tree(node, 1);
	fprintf(fp, "</assert>\n");
}

bool Aggregate::check(void) const {
	return eval(node) != 0;
}

float Aggregate::eval(const Node *n) const {
	OperatorNode *onode;
	switch (n->type()) {
		case NODE_INT:         return ((IntNode*)n)->value;
		case NODE_FLOAT:       return ((FloatNode*)n)->value;
		case NODE_OPERATOR:
			onode = (OperatorNode*)n;
			switch (onode->op) {
				case EQ:
					assert(onode->nops() == 2);
					return eval(onode->operands[0]) == eval(onode->operands[1]);
				case '<':
					assert(onode->nops() == 2);
					return eval(onode->operands[0]) < eval(onode->operands[1]);
				case '>':
					assert(onode->nops() == 2);
					return eval(onode->operands[0]) > eval(onode->operands[1]);
				case LE:
					assert(onode->nops() == 2);
					return eval(onode->operands[0]) <= eval(onode->operands[1]);
				case GE:
					assert(onode->nops() == 2);
					return eval(onode->operands[0]) >= eval(onode->operands[1]);
				case NE:
					assert(onode->nops() == 2);
					return eval(onode->operands[0]) != eval(onode->operands[1]);
				case IN:{
					assert(onode->nops() == 2);
					assert(onode->operands[1]->type() == NODE_OPERATOR);
					OperatorNode *right = (OperatorNode*)onode->operands[1];
					assert(right->op == RANGE);
					assert(right->nops() == 2);
					float min = eval(right->operands[0]);
					float max = eval(right->operands[1]);
					float test = eval(onode->operands[0]);
					return min <= test && test <= max;
				}
				case INSTANCES:
					assert(onode->nops() == 1);
					return 1;   //!!
				case AVERAGE:
					assert(onode->nops() == 2);
					return 0.1;   //!!
				case STDDEV:
					assert(onode->nops() == 2);
					return 0.1;   //!!
				case F_MAX:
					assert(onode->nops() == 2);
					return 0.1;   //!!
				case F_MIN:
					assert(onode->nops() == 2);
					return 0.1;   //!!
				case UNIQUE:
				default:
					fprintf(stderr, "Operator %s (%d) not implemented\n", get_op_name(onode->op), onode->op);
					abort();
			}
		default:
			fprintf(stderr, "Node type %d not implemented\n", n->type());
			abort();
	}
}
