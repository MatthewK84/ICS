// expect: AV 107:
// av107_block_scope_function.cpp: a JSF AV seed with a function declared at block scope.
namespace seed
{

void outer()
{
  void inner();
}

}  // namespace seed
