class Dispatcher
  def run(action)
    send(action)            # fully computed: any method of Dispatcher may be the target
  end

  def start = prepare       # uncertain: the send may call it
  def stop = nil            # uncertain

  private

  def prepare = Config::DEFAULTS # uncertain: reached only from start
end

module Config
  DEFAULTS = {}             # uncertain, and so is Config, which holds it
end

class Other
  def lonely = nil          # dead: the send is in another class
end

class Factory
  def self.build(kind) = send(kind) # class-method send: only class methods are uncertain
  def self.widget = nil     # uncertain
  def gadget = nil          # dead: an instance method, out of reach of the class-level send
end
