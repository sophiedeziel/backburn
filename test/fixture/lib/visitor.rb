class Visitor
  def accept(node)
    bump
    send(:"visit_#{node.class.name}", node)   # computed name: reaches every visit_* method
  end

  def visit_Integer(n) = n
  def visit_String(s) = s

  class_eval <<~RUBY
    def generated = helper_from_eval           # code in a class_eval string is read
  RUBY

  def helper_from_eval = 1
  def leftover = 2                             # really dead

  private

  def bump = self.counter += 1                 # reads counter, writes counter=
  def counter = @counter.to_i
  def counter=(v)
    @counter = v
  end
end

Visitor.new.accept(1)
